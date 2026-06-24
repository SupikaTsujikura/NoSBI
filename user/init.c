#include <string.h>

#include "lib.h"

#define INIT_SCRIPT_MAX 1024
#define INIT_ARG_MAX    64

static u_long init_strlen(const char *s) {
	u_long len = 0;

	while (s[len] != '\0') {
		len++;
	}
	return len;
}

static void init_puts(const char *s) {
	(void)syscall_print_cons(s, init_strlen(s));
}

static void init_write_str(int fd, const char *s) {
	(void)write(fd, s, init_strlen(s));
}

static void init_putnum(int value) {
	char buf[16];
	int i = 0;
	int neg = value < 0;
	unsigned int x = neg ? (unsigned int)(-value) : (unsigned int)value;

	if (neg) {
		init_puts("-");
	}
	do {
		buf[i++] = (char)('0' + x % 10);
		x /= 10;
	} while (x != 0 && i < (int)sizeof(buf));
	while (i > 0) {
		char ch = buf[--i];

		(void)syscall_print_cons(&ch, 1);
	}
}

static char *init_skip_space(char *s) {
	while (*s == ' ' || *s == '\t') {
		s++;
	}
	return s;
}

static void init_trim_right(char *s) {
	u_long n = init_strlen(s);

	while (n > 0 && (s[n - 1] == ' ' || s[n - 1] == '\t' || s[n - 1] == '\r' ||
	                 s[n - 1] == '\n')) {
		s[--n] = '\0';
	}
}

static char *init_next_token(char **cursor) {
	char *s = init_skip_space(*cursor);
	char *tok;

	if (*s == '\0') {
		*cursor = s;
		return 0;
	}
	tok = s;
	while (*s != '\0' && *s != ' ' && *s != '\t' && *s != '\r' && *s != '\n') {
		s++;
	}
	if (*s != '\0') {
		*s++ = '\0';
	}
	*cursor = s;
	return tok;
}

static int init_contains(const char *haystack, const char *needle) {
	u_long needle_len = init_strlen(needle);

	if (needle_len == 0) {
		return 1;
	}
	for (u_long i = 0; haystack[i] != '\0'; i++) {
		u_long j = 0;

		while (j < needle_len && haystack[i + j] != '\0' &&
		       haystack[i + j] == needle[j]) {
			j++;
		}
		if (j == needle_len) {
			return 1;
		}
	}
	return 0;
}

static int init_parse_octal(const char *s, u32 *value) {
	u32 out = 0;

	if (s == 0 || *s == '\0' || value == 0) {
		return -1;
	}
	while (*s != '\0') {
		if (*s < '0' || *s > '7') {
			return -1;
		}
		out = (out << 3) | (u32)(*s - '0');
		if (out > 0777) {
			return -1;
		}
		s++;
	}
	*value = out;
	return 0;
}

static void init_cat_to(const char *path, int out_fd) {
	char buf[128];
	int fd = open(path, 0);
	int n;

	if (fd < 0) {
		init_puts("[init-cat-miss:");
		init_puts(path);
		init_puts(":");
		init_putnum(fd);
		init_puts("]");
		return;
	}
	init_puts("[init-cat:");
	init_puts(path);
	init_puts(":");
	while ((n = read(fd, buf, sizeof(buf) - 1)) > 0) {
		(void)write(out_fd, buf, (u_long)n);
	}
	init_puts("]");
	(void)close(fd);
}

static void init_cat(const char *path) {
	init_cat_to(path, 1);
}

static void init_list_dir_to(const char *dir, int out_fd) {
	char path[FS_PATH_MAX];
	struct Stat st;

	init_write_str(out_fd, "[init-ls:");
	for (int i = 0; listdir(dir, i, path, sizeof(path), &st) > 0; i++) {
		(void)st;
		init_write_str(out_fd, path);
		init_write_str(out_fd, ",");
	}
	init_write_str(out_fd, "]");
}

static void init_list_dir(const char *dir) {
	init_list_dir_to(dir, 1);
}

static int init_echo_to(char *text, int out_fd) {
	char *redir = 0;
	int append = 0;

	for (char *p = text; *p != '\0'; p++) {
		if (*p == '>') {
			redir = p;
			if (p[1] == '>') {
				append = 1;
				p[1] = '\0';
			}
			*p = '\0';
			break;
		}
	}
	init_trim_right(text);
	if (redir == 0) {
		init_write_str(out_fd, text);
		init_write_str(out_fd, "\n");
		return 0;
	}
	{
		char *path = init_skip_space(redir + (append ? 2 : 1));
		int flags = FS_OPEN_CREATE | (append ? FS_OPEN_APPEND : FS_OPEN_TRUNC);
		int fd;

		init_trim_right(path);
		if (*path == '\0') {
			return -1;
		}
		fd = open(path, flags);
		if (fd < 0) {
			return fd;
		}
		(void)write(fd, text, init_strlen(text));
		(void)write(fd, "\n", 1);
		return close(fd);
	}
}

static int init_grep_fd(const char *needle, int in_fd, int out_fd) {
	char buf[128];
	char line[128];
	int line_len = 0;
	int n;

	if (needle == 0 || needle[0] == '\0') {
		return -1;
	}
	while ((n = read(in_fd, buf, sizeof(buf))) > 0) {
		for (int i = 0; i < n; i++) {
			char ch = buf[i];

			if (line_len + 1 < (int)sizeof(line)) {
				line[line_len++] = ch;
			}
			if (ch == '\n' || line_len + 1 >= (int)sizeof(line)) {
				line[line_len] = '\0';
				if (init_contains(line, needle)) {
					(void)write(out_fd, line, (u_long)line_len);
				}
				line_len = 0;
			}
		}
	}
	if (line_len > 0) {
		line[line_len] = '\0';
		if (init_contains(line, needle)) {
			(void)write(out_fd, line, (u_long)line_len);
		}
	}
	return 0;
}

static int init_setup_redir(char *cursor, int default_out, int *out_fd) {
	char *redir = 0;
	int append = 0;

	*out_fd = default_out;
	for (char *p = cursor; *p != '\0'; p++) {
		if (*p == '>') {
			redir = p;
			if (p[1] == '>') {
				append = 1;
				p[1] = '\0';
			}
			*p = '\0';
			break;
		}
	}
	if (redir != 0) {
		char *path = init_skip_space(redir + (append ? 2 : 1));
		int flags = FS_OPEN_CREATE | (append ? FS_OPEN_APPEND : FS_OPEN_TRUNC);

		init_trim_right(path);
		init_trim_right(cursor);
		if (*path == '\0') {
			return -1;
		}
		*out_fd = open(path, flags);
		return *out_fd < 0 ? *out_fd : 0;
	}
	return 0;
}

static int init_run_command(char *line, int in_fd, int default_out) {
	char *cursor;
	char *cmd;
	int out_fd;
	int close_out = 0;
	int r = 0;

	init_trim_right(line);
	cursor = init_skip_space(line);
	if (*cursor == '\0' || *cursor == '#') {
		return 0;
	}
	cmd = init_next_token(&cursor);
	if (cmd == 0) {
		return 0;
	}
	if (init_setup_redir(cursor, default_out, &out_fd) < 0) {
		return -1;
	}
	close_out = out_fd != default_out && out_fd >= 0;
	if (strcmp(cmd, "echo") == 0) {
		r = init_echo_to(init_skip_space(cursor), out_fd);
		goto out;
	}
	if (strcmp(cmd, "cat") == 0) {
		char *path = init_next_token(&cursor);

		if (path != 0) {
			init_cat_to(path, out_fd);
		} else {
			char buf[128];
			int n;

			while ((n = read(in_fd, buf, sizeof(buf))) > 0) {
				(void)write(out_fd, buf, (u_long)n);
			}
		}
		goto out;
	}
	if (strcmp(cmd, "grep") == 0) {
		char *needle = init_next_token(&cursor);

		r = init_grep_fd(needle, in_fd, out_fd);
		goto out;
	}
	if (strcmp(cmd, "ls") == 0) {
		char *path = init_next_token(&cursor);

		init_list_dir_to(path == 0 ? "/" : path, out_fd);
		goto out;
	}
	if (strcmp(cmd, "cd") == 0) {
		char *path = init_next_token(&cursor);

		return path == 0 ? -1 : chdir(path);
	}
	if (strcmp(cmd, "pwd") == 0) {
		char path[FS_PATH_MAX];

		if (getcwd(path, sizeof(path)) == 0) {
			init_puts("[init-pwd:");
			init_puts(path);
			init_puts("]");
			goto out;
		}
		r = -1;
		goto out;
	}
	if (strcmp(cmd, "mkdir") == 0) {
		char *path = init_next_token(&cursor);

		r = path == 0 ? -1 : mkdir(path);
		goto out;
	}
	if (strcmp(cmd, "rm") == 0) {
		char *path = init_next_token(&cursor);

		r = path == 0 ? -1 : remove(path);
		goto out;
	}
	if (strcmp(cmd, "mv") == 0) {
		char *old_path = init_next_token(&cursor);
		char *new_path = init_next_token(&cursor);

		r = old_path == 0 || new_path == 0 ? -1 : rename(old_path, new_path);
		goto out;
	}
	if (strcmp(cmd, "ln") == 0) {
		char *first = init_next_token(&cursor);
		char *old_path;
		char *new_path;
		int symbolic = first != 0 && strcmp(first, "-s") == 0;

		old_path = symbolic ? init_next_token(&cursor) : first;
		new_path = init_next_token(&cursor);
		r = old_path == 0 || new_path == 0 ? -1 :
		    (symbolic ? symlink(old_path, new_path) : link(old_path, new_path));
		goto out;
	}
	if (strcmp(cmd, "readlink") == 0) {
		char *path = init_next_token(&cursor);
		char target[FS_PATH_MAX];

		r = path == 0 ? -1 : readlink(path, target, sizeof(target) - 1);
		if (r >= 0) {
			target[r] = '\0';
			init_write_str(out_fd, target);
			init_write_str(out_fd, "\n");
			r = 0;
		}
		goto out;
	}
	if (strcmp(cmd, "chmod") == 0) {
		char *mode_text = init_next_token(&cursor);
		char *path = init_next_token(&cursor);
		u32 mode;

		r = mode_text == 0 || path == 0 ||
		    init_parse_octal(mode_text, &mode) < 0 ? -1 : chmod(path, mode);
		goto out;
	}
	if (strcmp(cmd, "run") == 0 || strcmp(cmd, "spawn") == 0) {
		char *path = init_next_token(&cursor);

		if (path != 0) {
			if (in_fd != 0) {
				(void)dup(in_fd, 0);
			}
			if (out_fd != 1) {
				(void)dup(out_fd, 1);
			}
			int child = spawn(path, 0);

			if (out_fd != 1) {
				(void)close(1);
			}
			if (in_fd != 0) {
				(void)close(0);
			}
			if (child >= 0) {
				init_puts("[init-run:");
				init_puts(path);
				init_puts("]");
				wait((u_long)child);
				goto out;
			}
		}
		r = -1;
		goto out;
	}
	init_puts("[init-unknown:");
	init_puts(cmd);
	init_puts("]");
	r = -1;
out:
	if (close_out) {
		(void)close(out_fd);
	}
	return r;
}

static int init_run_line(char *line) {
	char *pipe_pos = 0;

	for (char *p = line; *p != '\0'; p++) {
		if (*p == '|') {
			pipe_pos = p;
			break;
		}
	}
	if (pipe_pos != 0) {
		int pfd[2];
		int r;
		int child;

		*pipe_pos = '\0';
		if (pipe(pfd) < 0) {
			return -1;
		}
		child = fork();
		if (child == 0) {
			(void)close(pfd[0]);
			(void)init_run_command(line, 0, pfd[1]);
			(void)close(pfd[1]);
			exit();
		}
		if (child < 0) {
			(void)close(pfd[0]);
			(void)close(pfd[1]);
			return -1;
		}
		(void)close(pfd[1]);
		r = init_run_command(pipe_pos + 1, pfd[0], 1);
		(void)close(pfd[0]);
		wait((u_long)child);
		return r;
	}
	return init_run_command(line, 0, 1);
}

static int init_run_script(const char *path) {
	static char script[INIT_SCRIPT_MAX];
	char line[INIT_ARG_MAX];
	int fd;
	int n;
	int start = 0;

	fd = open(path, 0);
	if (fd < 0) {
		init_puts("[init-script-open-fail:");
		init_puts(path);
		init_puts(":");
		init_putnum(fd);
		init_puts("]");
		return fd;
	}
	n = read(fd, script, sizeof(script) - 1);
	(void)close(fd);
	if (n < 0) {
		return n;
	}
	script[n] = '\0';
	init_puts("[init-script:");
	init_puts(path);
	init_puts("]");
	for (int i = 0; i <= n; i++) {
		if (script[i] == '\n' || script[i] == '\0') {
			int len = i - start;

			if (len >= (int)sizeof(line)) {
				len = (int)sizeof(line) - 1;
			}
			for (int j = 0; j < len; j++) {
				line[j] = script[start + j];
			}
			line[len] = '\0';
			(void)init_run_line(line);
			start = i + 1;
		}
	}
	return 0;
}

static void init_probe_scripts(void) {
	static const char *scripts[] = {
	    "/init.rc",
	    "/basic_testcode.sh",
	    "/busybox_testcode.sh",
	    "/lua_testcode.sh",
	    "/libctest_testcode.sh",
	    "/libcbench_testcode.sh",
	    "/unixbench_testcode.sh",
	};
	struct Stat st;

	init_list_dir("/");
	for (u_long i = 0; i < sizeof(scripts) / sizeof(scripts[0]); i++) {
		if (stat(scripts[i], &st) == 0 && st.st_type == FS_TYPE_FILE) {
			int run_rc = 0;

			init_puts("[init-found:");
			init_puts(scripts[i]);
			init_puts("]");
			if (i < 2) {
				run_rc = init_run_script(scripts[i]);
				init_puts("[init-script-rc:");
				init_putnum(run_rc);
				init_puts("]");
			}
		}
	}
	if (stat("/disk.txt", &st) == 0) {
		init_cat("/disk.txt");
	}
}

static void init_fs_smoke(void) {
	char buf[64];
	struct Stat st;
	int fd;
	int n;

	(void)mkdir("/tmp/init.d");
	fd = open("/tmp/init.d/log.txt", FS_OPEN_CREATE | FS_OPEN_TRUNC);
	if (fd < 0) {
		init_puts("[init-fs-create-fail]");
		return;
	}
	(void)write(fd, "init-overlay", 12);
	(void)seek(fd, 0);
	n = read(fd, buf, sizeof(buf) - 1);
	(void)close(fd);
	if (n > 0) {
		buf[n] = '\0';
		init_puts("[init-read:");
		init_puts(buf);
		init_puts("]");
	}
	if (rename("/tmp/init.d/log.txt", "/tmp/init.d/renamed.txt") == 0 &&
	    stat("/tmp/init.d/renamed.txt", &st) == 0) {
		init_puts("[init-rename-ok]");
	}
	(void)remove("/tmp/init.d/renamed.txt");
	(void)remove("/tmp/init.d");
}

void user_main(long arg, char **argv) {
	int child;

	(void)arg;
	(void)argv;
	init_puts("[init-start]");
	init_fs_smoke();
	init_probe_scripts();
	child = syscall_spawn("/bin/demo", 'A');
	if (child < 0) {
		user_panic("init spawn demo failed");
	}
	init_puts("[init-spawn-demo]");
	for (;;) {
		syscall_yield();
	}
}
