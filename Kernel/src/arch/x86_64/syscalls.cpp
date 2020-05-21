#include <syscalls.h>

#include <idt.h>
#include <scheduler.h>
#include <logging.h>
#include <video.h>
#include <hal.h>
#include <fb.h>
#include <physicalallocator.h>
#include <gui.h>
#include <timer.h>
#include <pty.h>
#include <lock.h>
#include <lemon.h>
#include <sharedmem.h>
#include <cpu.h>
#include <net/socket.h>

#define SYS_EXIT 1
#define SYS_EXEC 2
#define SYS_READ 3
#define SYS_WRITE 4
#define SYS_OPEN 5
#define SYS_CLOSE 6
#define SYS_SLEEP 7
#define SYS_CREATE 8
#define SYS_LINK 9
#define SYS_UNLINK 10
#define SYS_CHDIR 12
#define SYS_TIME 13
#define SYS_MAP_FB 14
#define SYS_ALLOC 15
#define SYS_CHMOD 16
#define SYS_CREATE_DESKTOP 17
#define SYS_STAT 18
#define SYS_LSEEK 19
#define SYS_GETPID 20
#define SYS_MOUNT 21
#define SYS_CREATE_WINDOW 22
#define SYS_DESTROY_WINDOW 23
#define SYS_DESKTOP_GET_WINDOW 24

#define SYS_UPDATE_WINDOW 26
#define SYS_GET_DESKTOP_PID 27
#define SYS_SEND_MESSAGE 28
#define SYS_RECEIVE_MESSAGE 29
#define SYS_UPTIME 30
#define SYS_GET_VIDEO_MODE 31
#define SYS_UNAME 32
#define SYS_READDIR 33
#define SYS_SET_FS_BASE 34
#define SYS_MMAP 35
#define SYS_GRANT_PTY 36
#define SYS_GET_CWD 37
#define SYS_WAIT_PID 38
#define SYS_NANO_SLEEP 39
#define SYS_PREAD 40
#define SYS_PWRITE 41
#define SYS_IOCTL 42
#define SYS_INFO 43
#define SYS_MUNMAP 44
#define SYS_CREATE_SHARED_MEMORY 45
#define SYS_MAP_SHARED_MEMORY 46
#define SYS_UNMAP_SHARED_MEMORY 47
#define SYS_DESTROY_SHARED_MEMORY 48
#define SYS_SOCKET 49
#define SYS_BIND 50
#define SYS_LISTEN 51
#define SYS_ACCEPT 52
#define SYS_CONNECT 53
#define SYS_SEND 54
#define SYS_SENDTO 55
#define SYS_RECEIVE 56
#define SYS_RECEIVEFROM 57
#define SYS_GETUID 58
#define SYS_SETUID 59

#define NUM_SYSCALLS 60

#define EXEC_CHILD 1

typedef long(*syscall_t)(regs64_t*);

long SysExit(regs64_t* r){
	int64_t code = r->rbx;

	Log::Info("Process %d exiting with code %d", Scheduler::GetCurrentProcess()->pid, code);

	Scheduler::EndProcess(Scheduler::GetCurrentProcess());
	return 0;
}

long SysExec(regs64_t* r){
	char* filepath = (char*)kmalloc(strlen((char*)r->rbx) + 1);

	if(!Memory::CheckUsermodePointer(r->rbx, 0, Scheduler::GetCurrentProcess()->addressSpace)) return -1;

	strcpy(filepath, (char*)r->rbx);
	int argc = r->rcx;
	char** argv = (char**)r->rdx;
	uint64_t flags = r->rsi;
	char** envp = (char**)r->rdi;

	Log::Info("Executing: %s", (char*)r->rbx);

	FsNode* current_node = fs::ResolvePath(filepath, Scheduler::GetCurrentProcess()->workingDir);

	if(!current_node){
		return 1;
	}

	uint8_t* buffer = (uint8_t*)kmalloc(current_node->size);
	size_t read = fs::Read(current_node, 0, current_node->size, buffer);
	if(!read){
		Log::Warning("Could not read file: %s", current_node->name);
		return 0;
	}

	char** kernelArgv = (char**)kmalloc(argc * sizeof(char*));
	for(int i = 0; i < argc; i++){
		kernelArgv[i] = (char*)kmalloc(strlen(argv[i] + 1));
		strcpy(kernelArgv[i], argv[i]);
	}
	
	int envCount = 0;
	char** kernelEnvp = nullptr;

	if(envp){
		int i = 0;
        for(; envp[i]; i++);
		envCount = i;

		kernelEnvp = (char**)kmalloc(envCount * sizeof(char*));
		for(int i = 0; i < envCount; i++){
			kernelEnvp[i] = (char*)kmalloc(strlen(envp[i] + 1));
			strcpy(kernelEnvp[i], envp[i]);
			Log::Info("Environment Variable %s", envp[i]);
		}
	}

	process_t* proc = Scheduler::CreateELFProcess((void*)buffer, argc, kernelArgv, envCount, kernelEnvp);

	for(int i = 0; i < argc; i++){
		kfree(kernelArgv[i]);
	}
	
	kfree(kernelArgv);
	kfree(buffer);

	if(!proc) return 0;

	if(flags & EXEC_CHILD){
		Scheduler::GetCurrentProcess()->children.add_back(proc);
		proc->parent = Scheduler::GetCurrentProcess();

		proc->fileDescriptors.replace_at(0, Scheduler::GetCurrentProcess()->fileDescriptors.get_at(0));
		proc->fileDescriptors.replace_at(1, Scheduler::GetCurrentProcess()->fileDescriptors.get_at(1));
		proc->fileDescriptors.replace_at(2, Scheduler::GetCurrentProcess()->fileDescriptors.get_at(2));
	}

	strncpy(proc->workingDir, Scheduler::GetCurrentProcess()->workingDir, PATH_MAX);

	return proc->pid;
}

long SysRead(regs64_t* r){
	process_t* proc = Scheduler::GetCurrentProcess();
	if(r->rbx > proc->fileDescriptors.get_length()){
		Log::Warning("Invalid File Descriptor: %d", r->rbx);
		return -1;
	}
	fs_fd_t* handle = proc->fileDescriptors[r->rbx];
	if(!handle){
		Log::Warning("Invalid File Descriptor: %d", r->rbx);
		return -2;
	}

	uint8_t* buffer = (uint8_t*)r->rcx;
	uint64_t count = r->rdx;

	if(!Memory::CheckUsermodePointer(r->rcx, count, proc->addressSpace)){
		return -3;
	}

	int ret = fs::Read(handle, count, buffer);
	return ret;
}

long SysWrite(regs64_t* r){
	if(r->rbx > Scheduler::GetCurrentProcess()->fileDescriptors.get_length()){
		Log::Warning("Invalid File Descriptor: %d", r->rbx);
		return -1;
	}
	fs_fd_t* handle = Scheduler::GetCurrentProcess()->fileDescriptors[r->rbx];
	if(!handle){
		Log::Warning("Invalid File Descriptor: %d", r->rbx);
		return -2;
	}

	if(!(r->rcx && r->rdx)) return 1;

	int ret = fs::Write(handle, r->rdx, (uint8_t*)r->rcx);

	if(r->rsi){
		*((int*)r->rsi) = ret;
	}
	return ret;
}

long SysOpen(regs64_t* r){
	char* filepath = (char*)kmalloc(strlen((char*)r->rbx) + 1);
	strcpy(filepath, (char*)r->rbx);
	FsNode* root = fs::GetRoot();

	Log::Info("Opening: %s", filepath);
	long fd;
	if(strcmp(filepath,"/") == 0){
		fd = Scheduler::GetCurrentProcess()->fileDescriptors.get_length();
		Scheduler::GetCurrentProcess()->fileDescriptors.add_back(fs::Open(root, 0));
		return fd;
	}

	FsNode* node = fs::ResolvePath(filepath, Scheduler::GetCurrentProcess()->workingDir);

	if(!node){
		Log::Warning("Failed to open file!");
		return -1;
	}

	fd = Scheduler::GetCurrentProcess()->fileDescriptors.get_length();
	Scheduler::GetCurrentProcess()->fileDescriptors.add_back(fs::Open(node, 0));
	fs::Open(node, 0);

	return fd;
}

long SysClose(regs64_t* r){
	int fd = r->rbx;
	fs_fd_t* handle;
	if((handle = Scheduler::GetCurrentProcess()->fileDescriptors[fd])){
		fs::Close(handle);
	}
	Scheduler::GetCurrentProcess()->fileDescriptors.replace_at(fd, NULL);
	return 0;
}

long SysSleep(regs64_t* r){
	return 0;
}

long SysCreate(regs64_t* r){
	return 0;
}

long SysLink(regs64_t* r){
	return 0;
}

long SysUnlink(regs64_t* r){
	return 0;
}

long SysChdir(regs64_t* r){
	if(r->rbx){
		char* path =  fs::CanonicalizePath((char*)r->rbx, Scheduler::GetCurrentProcess()->workingDir);
		if(!fs::ResolvePath(path)) {
			Log::Warning("chdir: Could not find %s", path);
			return -1;
		}
		strcpy(Scheduler::GetCurrentProcess()->workingDir, path);
	} else Log::Warning("chdir: Invalid path string");
	return 0;
}

long SysTime(regs64_t* r){
	return 0;
}

long SysMapFB(regs64_t *r){
	video_mode_t vMode = Video::GetVideoMode();

	uint64_t pageCount = (vMode.height * vMode.pitch + 0xFFF) >> 12;
	uintptr_t fbVirt = (uintptr_t)Memory::Allocate4KPages(pageCount, Scheduler::GetCurrentProcess()->addressSpace);
	Memory::MapVirtualMemory4K(HAL::multibootInfo.framebufferAddr,fbVirt,pageCount,Scheduler::GetCurrentProcess()->addressSpace);

	mem_region_t memR;
	memR.base = fbVirt;
	memR.pageCount = pageCount;
	Scheduler::GetCurrentProcess()->sharedMemory.add_back(memR);

	fb_info_t fbInfo;
	fbInfo.width = vMode.width;
	fbInfo.height = vMode.height;
	fbInfo.bpp = vMode.bpp;
	fbInfo.pitch = vMode.pitch;

	if(HAL::debugMode) fbInfo.height = vMode.height / 3 * 2;

	Log::Info("Mapping Framebuffer to:");
	Log::Info(fbVirt);

	*((uintptr_t*)r->rbx) = fbVirt;
	*((fb_info_t*)r->rcx) = fbInfo;

	Log::Info(r->rbx);
	Log::Info(r->rcx);
	Log::Info(*((uint64_t*)r->rbx));

	return 0;
}

long SysAlloc(regs64_t* r){
	uint64_t pageCount = r->rbx;
	uintptr_t* addressPointer = (uintptr_t*)r->rcx;

	uintptr_t address = (uintptr_t)Memory::Allocate4KPages(pageCount, Scheduler::GetCurrentProcess()->addressSpace);

	assert(address);

	for(unsigned i = 0; i < pageCount; i++){
		Memory::MapVirtualMemory4K(Memory::AllocatePhysicalMemoryBlock(),address + i * PAGE_SIZE_4K,1,Scheduler::GetCurrentProcess()->addressSpace);
		memset((void*)(address + i * PAGE_SIZE_4K), 0, PAGE_SIZE_4K);
	}

	*addressPointer = address;
}

long SysChmod(regs64_t* r){
	return 0;
}

long SysStat(regs64_t* r){
	stat_t* stat = (stat_t*)r->rbx;
	int fd = r->rcx;
	int* ret = (int*)r->rdx;

	FsNode* node = Scheduler::GetCurrentProcess()->fileDescriptors.get_at(fd)->node;
	if(!node){
		Log::Warning("sys_stat: Invalid File Descriptor, %d", fd);
		*ret = 1;
		return 1;
	}

	stat->st_dev = 0;
	stat->st_ino = node->inode;
	
	if(node->flags & FS_NODE_DIRECTORY) stat->st_mode = S_IFDIR;
	if(node->flags & FS_NODE_FILE) stat->st_mode = S_IFREG;
	if(node->flags & FS_NODE_BLKDEVICE) stat->st_mode = S_IFBLK;
	if(node->flags & FS_NODE_CHARDEVICE) stat->st_mode = S_IFCHR;
	if(node->flags & FS_NODE_SYMLINK) stat->st_mode = S_IFLNK;

	stat->st_nlink = 0;
	stat->st_uid = node->uid;
	stat->st_gid = 0;
	stat->st_rdev = 0;
	stat->st_size = node->size;
	stat->st_blksize = 0;
	stat->st_blocks = 0;

	*ret = 0;

	return 0;
}

long SysLSeek(regs64_t* r){
	if(!(r->rsi)){
		Log::Warning("sys_lseek: Invalid Return Address");
		return -2;
	}

	long ret = 0;
	int fd = r->rbx;

	if(fd >= Scheduler::GetCurrentProcess()->fileDescriptors.get_length() || !Scheduler::GetCurrentProcess()->fileDescriptors[fd]){
		Log::Warning("sys_lseek: Invalid File Descriptor, %d", fd);
		return -1;
	}

	switch(r->rdx){
	case 0: // SEEK_SET
		ret = Scheduler::GetCurrentProcess()->fileDescriptors[fd]->pos = r->rcx;
		return ret;
		break;
	case 1: // SEEK_CUR
		ret = Scheduler::GetCurrentProcess()->fileDescriptors[fd]->pos;
		return ret;
		break;
	case 2: // SEEK_END
		ret = Scheduler::GetCurrentProcess()->fileDescriptors[fd]->pos = Scheduler::GetCurrentProcess()->fileDescriptors[fd]->node->size;
		return ret;
		break;
	default:
		Log::Info("Invalid seek: %s, mode: %d", Scheduler::GetCurrentProcess()->fileDescriptors[fd]->node->name, r->rdx);
		return -1; // Invalid seek mode
		break;
	}
	*(int64_t*)r->rsi = ret;
	return ret;
}

long SysGetPID(regs64_t* r){
	uint64_t* pid = (uint64_t*)r->rbx;

	*pid = Scheduler::GetCurrentProcess()->pid;
	
	return 0;
}

long SysMount(regs64_t* r){
	return 0;
}

long SysCreateDesktop(regs64_t* r){
	window_list_t** winList = (window_list_t**)r->rbx;

	desktop_t* desktop = (desktop_t*)kmalloc(sizeof(desktop_t));

	uint64_t pageCount = (sizeof(window_list_t) + WINDOW_COUNT_MAX * sizeof(handle_t) + 0xFFF) >> 12;
	window_list_t* kernelWindowList = (window_list_t*)Memory::KernelAllocate4KPages(pageCount);
	window_list_t* userWindowList = (window_list_t*)Memory::Allocate4KPages(pageCount, Scheduler::GetCurrentProcess()->addressSpace);
	for(unsigned i = 0; i < pageCount; i++){
		uintptr_t phys = Memory::AllocatePhysicalMemoryBlock();
		Memory::KernelMapVirtualMemory4K(phys, ((uintptr_t)kernelWindowList) + i * PAGE_SIZE_4K, 1);
		Memory::MapVirtualMemory4K(phys, ((uintptr_t)userWindowList) + i * PAGE_SIZE_4K, 1, Scheduler::GetCurrentProcess()->addressSpace);
	}

	kernelWindowList->maxWindowCount = WINDOW_COUNT_MAX;
	kernelWindowList->windowCount = 0;

	desktop->windows = kernelWindowList;
	desktop->pid = Scheduler::GetCurrentProcess()->pid;
	desktop->lock = 0;

	*winList = userWindowList;

	SetDesktop(desktop);

	return 0;
}

long SysCreateWindow(regs64_t* r){
	win_info_t* info = (win_info_t*)r->rbx;
	window_t* win = (window_t*)kmalloc(sizeof(window_t));
	handle_t handle =  Scheduler::RegisterHandle(win);
	info->handle = handle;
	info->ownerPID = Scheduler::GetCurrentProcess()->pid;
	win->info = *info;
	win->desktop = GetDesktop();
	acquireLock(&GetDesktop()->lock);
	
	while(GetDesktop()->windows->dirty == 2);

	window_list_t* windowList = GetDesktop()->windows;
	if(windowList->windowCount < windowList->maxWindowCount)
		windowList->windows[windowList->windowCount++] = info->handle;

	windowList->dirty = 1;
	
	releaseLock(&GetDesktop()->lock);

	return 0;
}

long SysDestroyWindow(regs64_t* r){
	handle_t handle = (handle_t)r->rbx;

	window_t* win;
	if((win = (window_t*)Scheduler::FindHandle(handle))){
		desktop_t* desktop = GetDesktop();
		acquireLock(&desktop->lock);

		
		for(int i = 0; i < GetDesktop()->windows->windowCount; i++){
			if(GetDesktop()->windows->windows[i] == win->info.handle){
				while(desktop->windows->dirty == 2);
				memcpy(&desktop->windows->windows[i], &desktop->windows->windows[i + 1], (desktop->windows->windowCount - i - 1));
				desktop->windows->windowCount--;
				desktop->windows->dirty = 1;
			}
		}

		releaseLock(&GetDesktop()->lock);
	} else return 2;

	return 0;
}

long SysDesktopGetWindow(regs64_t* r){
	win_info_t* winInfo = (win_info_t*)r->rbx;

	acquireLock(&GetDesktop()->lock);

	handle_t handle = (handle_t)r->rcx;
	window_t* win = (window_t*)Scheduler::FindHandle(handle);

	if(win) {
		*winInfo = win->info;
	} else {
		releaseLock(&GetDesktop()->lock);
		return -1;
	}

	releaseLock(&GetDesktop()->lock);

	return 0;
}

long SysUpdateWindow(regs64_t* r){
	handle_t handle = (handle_t*)r->rbx;
	win_info_t* info = (win_info_t*)r->rdx;

	Window* window = (Window*)Scheduler::FindHandle(handle);
	if(!window) return 2;

	if(info){ // Update Window Info
		win_info_t oldInfo = window->info;
		window->info = *info;

		if(window->info.handle != oldInfo.handle){
			window->info.handle = oldInfo.handle; // Do not allow a change in handle
			Log::Error("sys_update_window: Applications are NOT allowed to change window handles");
		}

		if(oldInfo.width != info->width || oldInfo.height != info->height){
			// TODO: Ensure that enough memory is allocated in the case of a resized window
			Log::Warning("sys_update_window: Window has been resized");
		}

		if(GetDesktop()->windows->dirty != 2) GetDesktop()->windows->dirty = 1; // Force the WM to refresh the background

		return 0;
	}

	return 1;
}

long SysGetDesktopPID(regs64_t* r){
	return GetDesktop()->pid;
}

// SendMessage(message_t* msg) - Sends an IPC message to a process
long SysSendMessage(regs64_t* r){
	uint64_t pid = r->rbx;
	uint64_t msg = r->rcx;
	uint64_t data = r->rdx;
	uint64_t data2 = r->rsi;

	message_t message;
	message.senderPID = Scheduler::GetCurrentProcess()->pid;
	message.recieverPID = pid;
	message.msg = msg;
	message.data = data;
	message.data2 = data2;

	return Scheduler::SendMessage(message); // Send the message
}

// RecieveMessage(message_t* msg) - Grabs next message on queue and copies it to msg
long SysReceiveMessage(regs64_t* r){
	if(!(r->rbx && r->rcx)) return 1; // Was given null pointers

	message_t* msg = (message_t*)r->rbx;
	uint64_t* queueSize = (uint64_t*)r->rcx;

	*queueSize = Scheduler::GetCurrentProcess()->messageQueue.get_length();
	*msg = Scheduler::RecieveMessage(Scheduler::GetCurrentProcess());

	return 0;
}

long SysUptime(regs64_t* r){
	uint64_t* seconds = (uint64_t*)r->rbx;
	uint64_t* milliseconds = (uint64_t*)r->rcx;
	if(seconds){
		*seconds = Timer::GetSystemUptime();
	}
	if(milliseconds){
		*milliseconds = ((double)Timer::GetTicks())/(Timer::GetFrequency()/1000.0);
	}
	return 0;
}

long SysDebug(regs64_t* r){
	Log::Info("%s, %d", (char*)r->rbx, r->rcx);
	return 0;
}

long SysGetVideoMode(regs64_t* r){
	video_mode_t vMode = Video::GetVideoMode();
	fb_info_t fbInfo;
	fbInfo.width = vMode.width;
	fbInfo.height = vMode.height;
	if(HAL::debugMode) fbInfo.height = vMode.height / 3 * 2;
	fbInfo.bpp = vMode.bpp;
	fbInfo.pitch = vMode.pitch;

	*((fb_info_t*)r->rbx) = fbInfo;

	return 0;
}

long SysUName(regs64_t* r){
	char* str = (char*)r->rbx;
	strcpy(str, Lemon::versionString);

	return 0;
}

long SysReadDir(regs64_t* r){
	if(!(r->rbx && r->rcx)) return 1;

	unsigned int fd = r->rbx;

	if(fd > Scheduler::GetCurrentProcess()->fileDescriptors.get_length()){
		if(r->rsi) *((int*)r->rsi) = 0;
		return 2;
	} 
	
	fs_dirent_t* direntPointer = (fs_dirent_t*)r->rcx;

	unsigned int count = r->rdx;

	if(!(Scheduler::GetCurrentProcess()->fileDescriptors[fd]->node->flags & FS_NODE_DIRECTORY)){
		if(r->rsi) *((int*)r->rsi) = 0;
		return 2;
	}

	int ret = fs::ReadDir(Scheduler::GetCurrentProcess()->fileDescriptors[fd], direntPointer, count);

	if(r->rsi) *((int*)r->rsi) = ret;
	return ret;
}

long SysSetFsBase(regs64_t* r){
	asm volatile ("wrmsr" :: "a"(r->rbx & 0xFFFFFFFF) /*Value low*/, "d"((r->rbx >> 32) & 0xFFFFFFFF) /*Value high*/, "c"(0xC0000100) /*Set FS Base*/);
	GetCPULocal()->currentThread->fsBase = r->rbx;
	return 0;
}

long SysMmap(regs64_t* r){
	uint64_t* address = (uint64_t*)r->rbx;
	size_t count = r->rcx;
	uintptr_t hint = r->rdx;

	uintptr_t _address;
	if(hint){
		if(Memory::CheckRegion(hint, count * PAGE_SIZE_4K, Scheduler::GetCurrentProcess()->addressSpace) /*Check availibilty of the requested map*/){
			_address = hint;
		} else {
			Log::Warning("sys_mmap: Could not map to address %x", hint);
			*address = 0;
			return 1;
		}
	} else _address = (uintptr_t)Memory::Allocate4KPages(count, Scheduler::GetCurrentProcess()->addressSpace);

	for(size_t i = 0; i < count; i++){
		Memory::MapVirtualMemory4K(Memory::AllocatePhysicalMemoryBlock(), _address + i * PAGE_SIZE_4K, 1, Scheduler::GetCurrentProcess()->addressSpace);
		memset((void*)(_address + i * PAGE_SIZE_4K), 0, PAGE_SIZE_4K);
	}

	*address = _address;

	return 0;
}

long SysGrantPTY(regs64_t* r){
	if(!r->rbx) return 1;

	PTY* pty = GrantPTY(Scheduler::GetCurrentProcess()->pid);

	process_t* currentProcess = Scheduler::GetCurrentProcess();

	*((int*)r->rbx) = currentProcess->fileDescriptors.get_length();
	
	currentProcess->fileDescriptors.replace_at(0, fs::Open(&pty->slaveFile)); // Stdin
	currentProcess->fileDescriptors.replace_at(1, fs::Open(&pty->slaveFile)); // Stdout
	currentProcess->fileDescriptors.replace_at(2, fs::Open(&pty->slaveFile)); // Stderr

	currentProcess->fileDescriptors.add_back(fs::Open(&pty->masterFile));

	return 0;
}

long SysGetCWD(regs64_t* r){
	char* buf = (char*)r->rbx;
	size_t sz = r->rcx;

	char* workingDir = Scheduler::GetCurrentProcess()->workingDir;
	if(strlen(workingDir) > sz) {
		return 1;
	} else {
		strcpy(buf, workingDir);
		return 0;
	}

	return 0;
}

long SysWaitPID(regs64_t* r){
	uint64_t pid = r->rbx;

	while(Scheduler::FindProcessByPID(pid)) { asm("hlt"); Scheduler::Yield(); }

	return 0;
}

long SysNanoSleep(regs64_t* r){
	uint64_t nanoseconds = r->rbx;

	uint64_t seconds = Timer::GetSystemUptime();
	uint64_t ticks = seconds * Timer::GetFrequency() + Timer::GetTicks();
	uint64_t ticksEnd = ticks + (int)(nanoseconds / (1000000000.0 / Timer::GetFrequency()));

	while((Timer::GetSystemUptime() * Timer::GetFrequency() + Timer::GetTicks()) < ticksEnd) { Scheduler::Yield(); }

	return 0;
}

long SysPRead(regs64_t* r){
	if(r->rbx > Scheduler::GetCurrentProcess()->fileDescriptors.get_length()){
		*((int*)r->rsi) = -1; // Return -1
		return -1;
	}
	FsNode* node;
	if(Scheduler::GetCurrentProcess()->fileDescriptors.get_at(r->rbx) || !(node = Scheduler::GetCurrentProcess()->fileDescriptors.get_at(r->rbx)->node)){ 
		Log::Warning("sys_pread: Invalid file descriptor: %d", r->rbx);
		return 1; 
	}
	uint8_t* buffer = (uint8_t*)r->rcx;
	if(!buffer) { return 1; }
	uint64_t count = r->rdx;
	uint64_t off = r->rdi;
	int ret = fs::Read(node, off, count, buffer);
	*(int*)r->rsi = ret;
	return 0;
}

long SysPWrite(regs64_t* r){
	if(r->rbx > Scheduler::GetCurrentProcess()->fileDescriptors.get_length()){
		*((int*)r->rsi) = -1; // Return -1
		return -1;
	}
	FsNode* node;
	if(Scheduler::GetCurrentProcess()->fileDescriptors.get_at(r->rbx) || !(node = Scheduler::GetCurrentProcess()->fileDescriptors.get_at(r->rbx)->node)){ 
		Log::Warning("sys_pwrite: Invalid file descriptor: %d", r->rbx);
		return 1; 
	}

	if(!(r->rcx)) {
		*((int*)r->rsi) = -1;
		return 1;
	}
	uint64_t off = r->rdi;

	int ret = fs::Write(node, off, r->rdx, (uint8_t*)r->rcx);

	if(r->rsi){
		*((int*)r->rsi) = ret;
	}

	return 0;
}

long SysIoctl(regs64_t* r){
	uint64_t fd = r->rbx;
	uint64_t request = r->rcx;
	uint64_t arg = r->rdx;
	int* result = (int*)r->rsi;

	if(fd >= Scheduler::GetCurrentProcess()->fileDescriptors.get_length()){
		return -1;
	}
	fs_fd_t* handle = Scheduler::GetCurrentProcess()->fileDescriptors[r->rbx];
	if(!handle){
		Log::Warning("sys_ioctl: Invalid File Descriptor: %d", r->rbx);
		return -2;
	}

	int ret = fs::Ioctl(handle, request, arg);

	if(result) *result = ret;

	return ret;
}

long SysInfo(regs64_t* r){
	lemon_sysinfo_t* s = (lemon_sysinfo_t*)r->rbx;

	if(!s){
		return -1;
	}

	s->usedMem = Memory::usedPhysicalBlocks * 4;
	s->totalMem = HAL::mem_info.memory_high + HAL::mem_info.memory_low;

	return 0;
}

/*
 * SysMunmap - Unmap memory (addr, count)
 * 
 * On success - return 0
 * On failure - return -1
 */
long SysMunmap(regs64_t* r){
	uint64_t address = r->rbx;
	size_t count = r->rcx;
	
	if(Memory::CheckRegion(address, count * PAGE_SIZE_4K, Scheduler::GetCurrentProcess()->addressSpace) /*Check availibilty of the requested map*/){
		//Memory::Free4KPages((void*)address, count, Scheduler::GetCurrentProcess()->addressSpace);
	} else {
		return -1;
	}

	return 0;
}

/* 
 * SysCreateSharedMemory (key, size, flags, recipient) - Create Shared Memory
 * key - Pointer to memory key
 * size - memory size
 * flags - flags
 * recipient - (if private flag) PID of the process that can access memory
 *
 * On Success - Return 0, key greater than 1
 * On Failure - Return -1, key null
 */
long SysCreateSharedMemory(regs64_t* r){
	uint64_t* key = (uint64_t*)r->rbx;
	uint64_t size = r->rcx;
	uint64_t flags = r->rdx;
	uint64_t recipient = r->rsi;

	*key = Memory::CreateSharedMemory(size, flags, Scheduler::GetCurrentProcess()->pid, recipient);

	if(!*key) return -1; // Failed

	return 0;
}

/* 
 * SysMapSharedMemory (ptr, key, hint) - Map Shared Memory
 * ptr - Pointer to pointer of mapped memory
 * key - Memory key
 * key - Address hint
 *
 * On Success - ptr > 0
 * On Failure - ptr = 0
 */
long SysMapSharedMemory(regs64_t* r){
	void** ptr = (void**)r->rbx;
	uint64_t key = r->rcx;
	uint64_t hint = r->rdx;

	*ptr = Memory::MapSharedMemory(key,Scheduler::GetCurrentProcess(), hint);

	return 0;
}

/* 
 * SysUnmapSharedMemory (address, key) - Map Shared Memory
 * address - address of mapped memory
 * key - Memory key
 *
 * On Success - return 0
 * On Failure - return -1
 */
long SysUnmapSharedMemory(regs64_t* r){
	uint64_t address = r->rbx;
	uint64_t key = r->rcx;

	shared_mem_t* sMem = Memory::GetSharedMemory(key);
	if(!sMem) return -1;

	if(!Memory::CheckRegion(address, sMem->pgCount * PAGE_SIZE_4K, Scheduler::GetCurrentProcess()->addressSpace)) // Make sure the process is not screwing with kernel memory
		return -1;

	Memory::Free4KPages((void*)address, sMem->pgCount, Scheduler::GetCurrentProcess()->addressSpace);

	return 0;
}

/* 
 * SysDestroySharedMemory (key) - Destroy Shared Memory
 * key - Memory key
 *
 * On Success - return 0
 * On Failure - return -1
 */
long SysDestroySharedMemory(regs64_t* r){
	uint64_t key = r->rbx;

	if(Memory::CanModifySharedMemory(Scheduler::GetCurrentProcess()->pid, key)){
		Memory::DestroySharedMemory(key);
	} else return -1;

	return 0;
}

/* 
 * SysSocket (domain, type, protocol) - Create socket
 * domain - socket domain
 * type - socket type
 * protcol - socket protocol
 *
 * On Success - return file descriptor
 * On Failure - return -1
 */
long SysSocket(regs64_t* r){
	int domain = r->rbx;
	int type = r->rcx;
	int protocol = r->rdx;

	Socket* sock = Socket::CreateSocket(domain, type, protocol);
	Log::Info("Socket: Domain: %d", sock->GetDomain());

	if(!sock) return -1;
	
	fs_fd_t* fDesc = fs::Open(sock, 0);
	int fd = Scheduler::GetCurrentProcess()->fileDescriptors.get_length();

	Scheduler::GetCurrentProcess()->fileDescriptors.add_back(fDesc);

	return fd;
}

/* 
 * SysBind (sockfd, addr, addrlen) - Bind address to socket
 * sockfd - Socket file descriptor
 * addr - sockaddr structure
 * addrlen - size of addr
 *
 * On Success - return 0
 * On Failure - return -1
 */
long SysBind(regs64_t* r){
	process_t* proc = Scheduler::GetCurrentProcess();
	fs_fd_t* handle = proc->fileDescriptors.get_at(r->rbx);
	if(!handle){ 
		Log::Warning("sys_bind: Invalid file descriptor: ", r->rbx);
		return -1; 
	}

	if(!(handle->node->flags & FS_NODE_SOCKET)){
		Log::Warning("sys_bind: File (Descriptor: %d) is not a socket", r->rbx);
		return -2;
	}

	socklen_t len = r->rdx;
	
	sockaddr_t* addr = (sockaddr_t*)r->rcx;
	if(!Memory::CheckUsermodePointer(r->rcx, len, proc->addressSpace)){
		Log::Warning("sys_bind: Invalid sockaddr ptr");
		return -3;
	}

	Socket* sock = (Socket*)handle->node;
	return sock->Bind(addr, len);
}

/* 
 * SysListen (sockfd, backlog) - Marks socket as passive
 * sockfd - socket file descriptor
 * backlog - maximum amount of pending connections
 *
 * On Success - return 0
 * On Failure - return -1
 */
long SysListen(regs64_t* r){
	process_t* proc = Scheduler::GetCurrentProcess();
	fs_fd_t* handle = proc->fileDescriptors.get_at(r->rbx);
	if(!handle){ 
		Log::Warning("sys_listen: Invalid file descriptor: ", r->rbx);
		return -1; 
	}

	if(!(handle->node->flags & FS_NODE_SOCKET)){
		Log::Warning("sys_listen: File (Descriptor: %d) is not a socket", r->rbx);
		return -2;
	}

	Socket* sock = (Socket*)handle->node;
	return sock->Listen(r->rcx);
}

/* 
 * SysAccept (sockfd, addr, addrlen) - Accept socket connection
 * sockfd - Socket file descriptor
 * addr - sockaddr structure
 * addrlen - size of addr
 *
 * On Success - return file descriptor of accepted socket
 * On Failure - return -1
 */
long SysAccept(regs64_t* r){
	process_t* proc = Scheduler::GetCurrentProcess();
	fs_fd_t* handle = proc->fileDescriptors.get_at(r->rbx);
	if(!handle){ 
		Log::Warning("sys_accept: Invalid file descriptor: ", r->rbx);
		return -1; 
	}

	if(!(handle->node->flags & FS_NODE_SOCKET)){
		Log::Warning("sys_accept: File (Descriptor: %d) is not a socket", r->rbx);
		return -2;
	}

	socklen_t* len = (socklen_t*)r->rdx;
	if(!Memory::CheckUsermodePointer(r->rdx, sizeof(socklen_t), proc->addressSpace)){
		Log::Warning("sys_accept: Invalid socklen ptr");
		return -3;
	}
	
	sockaddr_t* addr = (sockaddr_t*)r->rcx;
	if(!Memory::CheckUsermodePointer(r->rcx, *len, proc->addressSpace)){
		Log::Warning("sys_accept: Invalid sockaddr ptr");
		return -3;
	}

	Socket* sock = (Socket*)handle->node;
	
	Socket* newSock = sock->Accept(addr, len);
	int fd = proc->fileDescriptors.get_length();
	proc->fileDescriptors.add_back(fs::Open(newSock));

	if(newSock)
		return fd;
	else
		return -1;
}

/* 
 * SysConnect (sockfd, addr, addrlen) - Initiate socket connection
 * sockfd - Socket file descriptor
 * addr - sockaddr structure
 * addrlen - size of addr
 *
 * On Success - return 0
 * On Failure - return -1
 */
long SysConnect(regs64_t* r){
	process_t* proc = Scheduler::GetCurrentProcess();
	fs_fd_t* handle = proc->fileDescriptors.get_at(r->rbx);
	if(!handle){ 
		Log::Warning("sys_connect: Invalid file descriptor: ", r->rbx);
		return -1; 
	}

	if(!(handle->node->flags & FS_NODE_SOCKET)){
		Log::Warning("sys_connect: File (Descriptor: %d) is not a socket", r->rbx);
		return -2;
	}

	socklen_t len = r->rdx;
	
	sockaddr_t* addr = (sockaddr_t*)r->rcx;
	if(!Memory::CheckUsermodePointer(r->rcx, len, proc->addressSpace)){
		Log::Warning("sys_connect: Invalid sockaddr ptr");
		return -3;
	}

	Socket* sock = (Socket*)handle->node;
	return sock->Connect(addr, len);
}

/* 
 * SysSend (sockfd, buf, len, flags) - Send data through a socket
 * sockfd - Socket file descriptor
 * buf - data
 * len - data length
 * flags - flags
 *
 * On Success - return amount of data sent
 * On Failure - return -1
 */
long SysSend(regs64_t* r){
	process_t* proc = Scheduler::GetCurrentProcess();
	fs_fd_t* handle = proc->fileDescriptors.get_at(r->rbx);

	uint8_t* buffer = (uint8_t*)(r->rcx);
	size_t len = r->rdx;
	uint64_t flags = r->rsi;

	if(!handle){ 
		Log::Warning("sys_send: Invalid file descriptor: ", r->rbx);
		return -1; 
	}

	if(!(handle->node->flags & FS_NODE_SOCKET)){
		Log::Warning("sys_send: File (Descriptor: %d) is not a socket", r->rbx);
		return -2;
	}
	
	if(!Memory::CheckUsermodePointer(r->rcx, len, proc->addressSpace)){
		Log::Warning("sys_send: Invalid buffer ptr");
		return -3;
	}

	Socket* sock = (Socket*)handle->node;
	return sock->Send(buffer, len, flags);
}

/* 
 * SysSendTo (sockfd, buf, len, flags, destaddr, addrlen) - Send data through a socket
 * sockfd - Socket file descriptor
 * buf - data
 * len - data length
 * destaddr - Destination address
 * addrlen - Address length
 *
 * On Success - return amount of data sent
 * On Failure - return -1
 */
long SysSendTo(regs64_t* r){
	process_t* proc = Scheduler::GetCurrentProcess();
	fs_fd_t* handle = proc->fileDescriptors.get_at(r->rbx);

	uint8_t* buffer = (uint8_t*)(r->rcx);
	size_t len = r->rdx;
	uint64_t flags = r->rsi;

	if(!handle){ 
		Log::Warning("sys_send: Invalid file descriptor: ", r->rbx);
		return -1; 
	}

	if(!(handle->node->flags & FS_NODE_SOCKET)){
		Log::Warning("sys_send: File (Descriptor: %d) is not a socket", r->rbx);
		return -2;
	}
	
	if(!Memory::CheckUsermodePointer(r->rcx, len, proc->addressSpace)){
		Log::Warning("sys_send: Invalid buffer ptr");
		return -3;
	}
	
	socklen_t slen = r->rdx;
	
	sockaddr_t* addr = (sockaddr_t*)r->rcx;

	Socket* sock = (Socket*)handle->node;
	return sock->SendTo(buffer, len, flags, addr, slen);
}

/* 
 * SysReceive (sockfd, buf, len, flags) - Receive data through a socket
 * sockfd - Socket file descriptor
 * buf - data
 * len - data length
 * flags - flags
 *
 * On Success - return amount of data read
 * On Failure - return -1
 */
long SysReceive(regs64_t* r){
	process_t* proc = Scheduler::GetCurrentProcess();
	fs_fd_t* handle = proc->fileDescriptors.get_at(r->rbx);

	uint8_t* buffer = (uint8_t*)(r->rcx);
	size_t len = r->rdx;
	uint64_t flags = r->rsi;

	if(!handle){ 
		Log::Warning("sys_send: Invalid file descriptor: ", r->rbx);
		return -1; 
	}

	if(!(handle->node->flags & FS_NODE_SOCKET)){
		Log::Warning("sys_send: File (Descriptor: %d) is not a socket", r->rbx);
		return -2;
	}
	
	if(!Memory::CheckUsermodePointer(r->rcx, len, proc->addressSpace)){
		Log::Warning("sys_send: Invalid buffer ptr");
		return -3;
	}

	Socket* sock = (Socket*)handle->node;
	return sock->Receive(buffer, len, flags);
}

/* 
 * SysReceiveFrom (sockfd, buf, len, flags, srcaddr, addrlen) - Receive data through a socket
 * sockfd - Socket file descriptor
 * buf - data
 * len - data length
 * srcaddr - Source address
 * addrlen - Address length
 *
 * On Success - return amount of data read
 * On Failure - return -1
 */
long SysReceiveFrom(regs64_t* r){
	process_t* proc = Scheduler::GetCurrentProcess();
	fs_fd_t* handle = proc->fileDescriptors.get_at(r->rbx);

	uint8_t* buffer = (uint8_t*)(r->rcx);
	size_t len = r->rdx;
	uint64_t flags = r->rsi;

	if(!handle){ 
		Log::Warning("sys_send: Invalid file descriptor: ", r->rbx);
		return -1; 
	}

	if(!(handle->node->flags & FS_NODE_SOCKET)){
		Log::Warning("sys_send: File (Descriptor: %d) is not a socket", r->rbx);
		return -2;
	}
	
	if(!Memory::CheckUsermodePointer(r->rcx, len, proc->addressSpace)){
		Log::Warning("sys_send: Invalid buffer ptr");
		return -3;
	}
	
	socklen_t* slen = (socklen_t*)r->rdx;
	
	sockaddr_t* addr = (sockaddr_t*)r->rcx;

	Socket* sock = (Socket*)handle->node;
	return sock->ReceiveFrom(buffer, len, flags, addr, slen);
}

/* 
 * SetGetUID () - Get Process UID
 * 
 * On Success - Return process UID
 * On Failure - Does not fail
 */
long SysGetUID(regs64_t* r){
	return Scheduler::GetCurrentProcess()->uid;
}

long SysSetUID(regs64_t* r){
	
}

syscall_t syscalls[]{
	SysDebug,
	SysExit,					// 1
	SysExec,
	SysRead,
	SysWrite,
	SysOpen,					// 5
	SysClose,
	SysSleep,
	SysCreate,
	SysLink,
	SysUnlink,					// 10
	SysExec,
	SysChdir,
	SysTime,
	SysMapFB,
	SysAlloc,					// 15
	SysChmod,
	SysCreateDesktop,
	SysStat,
	SysLSeek,
	SysGetPID,					// 20
	SysMount,
	SysCreateWindow,
	SysDestroyWindow,
	SysDesktopGetWindow,
	nullptr,					// 25
	SysUpdateWindow,
	SysGetDesktopPID,
	SysSendMessage,
	SysReceiveMessage,
	SysUptime,					// 30
	SysGetVideoMode,
	SysUName,
	SysReadDir,
	SysSetFsBase,
	SysMmap,					// 35
	SysGrantPTY,
	SysGetCWD,
	SysWaitPID,
	SysNanoSleep,
	SysPRead,					// 40
	SysPWrite,
	SysIoctl,
	SysInfo,
	SysMunmap,
	SysCreateSharedMemory,		// 45
	SysMapSharedMemory,
	SysUnmapSharedMemory,
	SysDestroySharedMemory,
	SysSocket,
	SysBind,					// 50
	SysListen,
	SysAccept,
	SysConnect,
	SysSend,
	SysSendTo,					// 55
	SysReceive,
	SysReceiveFrom,
	SysGetUID,
	SysSetUID,					
};

int lastSyscall = 0;
void SyscallHandler(regs64_t* regs) {
	if (regs->rax >= NUM_SYSCALLS) // If syscall is non-existant then return
		return;
		
	asm("sti");

	long ret;
	if(syscalls[regs->rax])
		ret = syscalls[regs->rax](regs); // Call syscall
	regs->rax = ret;
}

void InitializeSyscalls() {
	IDT::RegisterInterruptHandler(0x69,SyscallHandler);
}