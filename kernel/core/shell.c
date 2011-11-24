/*
 * vim:tabstop=4
 * vim:noexpandtab
 *
 * ToAruOS Kernel Debugger Shell
 *
 * Part of the ToAruOS Kernel, under the NCSA license
 *
 * Copyright 2011 Kevin Lange
 *
 *   (Preliminary documentation based on intended future use; currently,
 *    this is just a file system explorer)
 *
 * This is a kernel debugging shell that allows basic, sh-like operation
 * of the system while it is in use, without other tasks running in the
 * background. While the debug shell is running, the tasker is disabled
 * and the kernel will remainin on its current task, allowing users to
 * display registry and memory information relavent to the current task.
 *
 */
#include <system.h>
#include <fs.h>
#include <multiboot.h>
#include <ata.h>

struct {
	char path[1024];
	char * username;
	char * hostname;
	uint16_t month, day, hours, minutes, seconds;
	fs_node_t * node;
} shell;

#define SHELL_COMMANDS 512
typedef uint32_t(*shell_command_t) (int argc, char ** argv);
char * shell_commands[SHELL_COMMANDS];
shell_command_t shell_pointers[SHELL_COMMANDS];
uint32_t shell_commands_len = 0;

void
redraw_shell() {
	kprintf("\033[1m[\033[1;33m%s \033[1;32m%s \033[1;31m%d/%d \033[1;34m%d:%d:%d\033[0m \033[0m%s\033[1m]\033[0m\n\033[1;32m$\033[0m ",
			shell.username, shell.hostname, shell.month, shell.day, shell.hours, shell.minutes, shell.seconds, shell.path);
}

void
init_shell() {
	shell.node = fs_root;
	shell.username = "kernel";
	shell.hostname = "toaru";
	shell.path[0]  = '/';
	shell.path[1]  = '\0';
}

void
shell_install_command(char * name, shell_command_t func) {
	if (shell_commands_len == SHELL_COMMANDS) {
		kprintf("Ran out of space for static shell commands. The maximum number of commands is %d\n", SHELL_COMMANDS);
		return;
	}
	shell_commands[shell_commands_len] = name;
	shell_pointers[shell_commands_len] = func;
	shell_commands_len++;
}

shell_command_t shell_find(char * str) {
	for (uint32_t i = 0; i < shell_commands_len; ++i) {
		if (!strcmp(str, shell_commands[i])) {
			return shell_pointers[i];
		}
	}
	return NULL;
}

void shell_update_time() {
	get_date(&shell.month, &shell.day);
	get_time(&shell.hours, &shell.minutes, &shell.seconds);
}

void shell_exec(char * buffer, int size) {
	/*
	 * Tokenize the command
	 */
	char * pch;
	char * cmd;
	char * save;
	pch = strtok_r(buffer," ",&save);
	cmd = pch;
	if (!cmd) { return; }
	char * argv[1024]; /* Command tokens (space-separated elements) */
	int tokenid = 0;
	while (pch != NULL) {
		argv[tokenid] = (char *)pch;
		++tokenid;
		pch = strtok_r(NULL," ",&save);
	}
	argv[tokenid] = NULL;
	shell_command_t func = shell_find(argv[0]);
	if (func) {
		func(tokenid, argv);
	} else {
		/* Alright, here we go */
		char * filename = malloc(sizeof(char) * 1024);
		fs_node_t * chd = NULL;
		if (argv[0][0] == '/') {
			memcpy(filename, argv[0], strlen(argv[0]) + 1);
			chd = kopen(filename, 0);
		}
		if (!chd) {
			/* Alright, let's try this... */
			char * search_path = "/bin/";
			memcpy(filename, search_path, strlen(search_path));
			memcpy((void*)((uintptr_t)filename + strlen(search_path)),argv[0],strlen(argv[0])+1);
			chd = kopen(filename, 0);
		}
		if (!chd) {
			kprintf("Unrecognized command: %s\n", cmd);
		} else {
			close_fs(chd);
			system(filename, tokenid, argv);
		}
		free(filename);
	}
}

uint32_t shell_cmd_cd(int argc, char * argv[]) {
	if (argc < 2) {
		return 1;
	} else {
		if (!strcmp(argv[1],".")) {
			return 0;
		} else {
			if (!strcmp(argv[1],"..")) {
				char * last_slash = (char *)rfind(shell.path,'/');
				if (last_slash == shell.path) { 
					last_slash[1] = '\0';
				} else {
					last_slash[0] = '\0';
				}
				shell.node = kopen(shell.path, 0);
			} else {
				char * filename = malloc(sizeof(char) * 1024);
				if (argv[1][0] == '/') {
					memcpy(filename, argv[1], strlen(argv[1]) + 1);
				} else {
					memcpy(filename, shell.path, strlen(shell.path));
					if (!strcmp(shell.path,"/")) {
						memcpy((void *)((uintptr_t)filename + strlen(shell.path)),argv[1],strlen(argv[1])+1); 
					} else {
						filename[strlen(shell.path)] = '/';
						memcpy((void *)((uintptr_t)filename + strlen(shell.path) + 1),argv[1],strlen(argv[1])+1); 
					}
				}
				fs_node_t * chd = kopen(filename, 0);
				if (chd) {
					if ((chd->flags & FS_DIRECTORY) == 0) {
						kprintf("cd: %s is not a directory\n", filename);
						return 1;
					}
					shell.node = chd;
					memcpy(shell.path, filename, strlen(filename));
					shell.path[strlen(filename)] = '\0';
				} else {
					kprintf("cd: could not change directory\n");
				}
			}
			for (uint32_t i = 0; i <= strlen(shell.path); ++i) {
				current_task->wd[i] = shell.path[i];
			}
		}
	}
	return 0;
}

uint32_t shell_cmd_info(int argc, char * argv[]) {
	if (argc < 2) {
		kprintf("info: Expected argument\n");
		return 1;
	}
	fs_node_t * file = kopen(argv[1], 0);
	if (!file) {
		kprintf("Could not open file `%s`\n", argv[1]);
		return 1;
	}
	kprintf("flags:   0x%x\n", file->flags);
	kprintf("mask:    0x%x\n", file->mask);
	kprintf("inode:   0x%x\n", file->inode);
	kprintf("uid: %d gid: %d\n", file->uid, file->gid);
	kprintf("open():  0x%x\n", file->open);
	kprintf("read():  0x%x\n", file->read);
	kprintf("write(): 0x%x\n", file->write);
	if ((file->mask & 0x001) || (file->mask & 0x008) || (file->mask & 0x040)) {
		kprintf("File is executable.\n");
	}
	close_fs(file);
	return 0;
}

uint32_t shell_cmd_ls(int argc, char * argv[]) {
	/*
	 * List the files in the current working directory
	 */
	struct dirent * entry = NULL;
	int i = 0;
	fs_node_t * ls_node;
	if (argc < 2) {
		ls_node = shell.node;
	} else {
		ls_node = kopen(argv[1], 0);
		if (!ls_node) {
			kprintf("Could not stat directory '%s'.\n", argv[1]);
			return 1;
		}
	}
	entry = readdir_fs(ls_node, i);
	while (entry != NULL) {
		char * filename = malloc(sizeof(char) * 1024);
		memcpy(filename, shell.path, strlen(shell.path));
		if (!strcmp(shell.path,"/")) {
			memcpy((void *)((uintptr_t)filename + strlen(shell.path)),entry->name,strlen(entry->name)+1); 
		} else {
			filename[strlen(shell.path)] = '/';
			memcpy((void *)((uintptr_t)filename + strlen(shell.path) + 1),entry->name,strlen(entry->name)+1); 
		}
		fs_node_t * chd = kopen(filename, 0);
		if (chd) {
			if (chd->flags & FS_DIRECTORY) {
				kprintf("\033[1;34m");
			} else if ((chd->mask & 0x001) || (chd->mask & 0x008) || (chd->mask & 0x040)) {
				kprintf("\033[1;32m");
			}
			close_fs(chd);
		}
		free(filename);
		kprintf("%s\033[0m\n", entry->name);
		free(entry);
		i++;
		entry = readdir_fs(ls_node, i);
	}
	if (ls_node != shell.node) {
		close_fs(ls_node);
	}
	return 0;
}

uint32_t shell_cmd_out(int argc, char * argv[]) {
	if (argc < 3) {
		kprintf("Need a port and a character (both as numbers, please) to write...\n");
		return 1;
	} else {
		int port;
		port = atoi(argv[1]);
		int val;
		val  = atoi(argv[2]);
		kprintf("Writing %d (%c) to port %d\n", val, (unsigned char)val, port);
		outportb((short)port, (unsigned char)val);
	}
	return 0;
}

uint32_t shell_cmd_cpudetect(int argc, char * argv[]) {
	detect_cpu();
	return 0;
}

uint32_t shell_cmd_multiboot(int argc, char * argv[]) {
	dump_multiboot(mboot_ptr);
	return 0;
}

uint32_t shell_cmd_screenshot(int argc, char * argv[]) {
	bochs_screenshot();
	return 0;
}

uint32_t shell_cmd_readsb(int argc, char * argv[]) {
	extern void ext2_disk_read_superblock();
	ext2_disk_read_superblock();
	return 0;
}

uint32_t shell_cmd_readdisk(int argc, char * argv[]) {
	uint8_t buf[512] = {1};
	uint32_t i = 0;
	uint8_t slave = 0;
	if (argc >= 2) {
		if (!strcmp(argv[1], "slave")) {
			slave = 1;
		}
	}
	while (buf[0]) {
		ide_read_sector(0x1F0, slave, i, buf);
		for (uint16_t j = 0; j < 512; ++j) {
			ansi_put(buf[j]);
		}
		++i;
	}
	return 0;
}

uint32_t shell_cmd_writedisk(int argc, char * argv[]) {
	uint8_t buf[512] = "Hello world!\n";
	ide_write_sector(0x1F0, 0, 0x000000, buf);
	return 0;
}

void install_commands() {
	shell_install_command("cd",         shell_cmd_cd);
	shell_install_command("ls",         shell_cmd_ls);
	shell_install_command("info",       shell_cmd_info);
	shell_install_command("out",        shell_cmd_out);
	shell_install_command("cpu-detect", shell_cmd_cpudetect);
	shell_install_command("multiboot",  shell_cmd_multiboot);
	shell_install_command("screenshot", shell_cmd_screenshot);
	shell_install_command("read-sb",    shell_cmd_readsb);
	shell_install_command("read-disk",  shell_cmd_readdisk);
	shell_install_command("write-disk", shell_cmd_writedisk);
}

void
start_shell() {
	init_shell();
	install_commands();
	while (1) {
		/* Read buffer */
		shell_update_time();
		redraw_shell();
		char buffer[1024];
		int size;
		/* Read commands */
		size = kgets((char *)&buffer, 1023);
		if (size < 1) {
			continue;
		} else {
			/* Execute command */
			shell_exec(buffer, size);
		}
	}
}

