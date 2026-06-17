#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* 将 `path` 所指文件的全部内容读入堆分配的缓冲区。
   成功时返回以 '\0' 结尾的缓冲区, 并可选地将文件大小写入 *size_out;
   失败时打印错误信息并返回 NULL。 */
static char *read_all(const char *path, long *size_out) {
	/* 以二进制模式打开文件 */
	FILE *fp = fopen(path, "rb");
	if (fp == NULL) {
		perror(path);
		return NULL;
	}
	/* 定位到文件末尾以获取文件大小 */
	if (fseek(fp, 0, SEEK_END) != 0) {
		perror("fseek");
		fclose(fp);
		return NULL;
	}
	long size = ftell(fp);
	if (size < 0) {
		perror("ftell");
		fclose(fp);
		return NULL;
	}
	/* 回到文件起始位置, 准备读取 */
	if (fseek(fp, 0, SEEK_SET) != 0) {
		perror("fseek");
		fclose(fp);
		return NULL;
	}
	/* 分配缓冲区: 文件大小 + 1 字节用于存放 '\0' */
	char *buf = malloc((size_t)size + 1);
	if (buf == NULL) {
		fprintf(stderr, "malloc failed\n");
		fclose(fp);
		return NULL;
	}
	/* 一次性读取整个文件 */
	size_t got = fread(buf, 1, (size_t)size, fp);
	if (got != (size_t)size) {
		fprintf(stderr, "short read from %s\n", path);
		free(buf);
		fclose(fp);
		return NULL;
	}
	/* 添加 '\0' 终止符, 使缓冲区可作为 C 字符串使用 */
	buf[size] = '\0';
	fclose(fp);
	/* 可选: 将文件大小报告给调用者 */
	if (size_out) {
		*size_out = size;
	}
	return buf;
}

/* 检查 haystack 中是否按顺序依次包含 needles 数组中的所有字符串。
   返回 1 表示全部按序找到, 返回 0 表示有缺失。 */
static int require_in_order(const char *haystack, const char **needles, size_t count) {
	const char *cursor = haystack;
	for (size_t i = 0; i < count; i++) {
		/* 从当前位置向后搜索第 i 个目标串 */
		const char *hit = strstr(cursor, needles[i]);
		if (hit == NULL) {
			fprintf(stderr, "missing required text: %s\n", needles[i]);
			return 0;
		}
		/* 推进游标到匹配位置之后, 保证后续搜索在本次匹配之后进行 */
		cursor = hit + strlen(needles[i]);
	}
	return 1;
}

/* 统计字符串 s 中字符 ch 出现的次数 */
static int count_char(const char *s, char ch) {
	int n = 0;
	for (; *s; s++) {
		if (*s == ch) {
			n++;
		}
	}
	return n;
}

/* 清洗原始日志: 去除回车符 '\r', 将空字符 '\0' 替换为 '?',
   其余字符原样保留。返回新分配的以 '\0' 结尾的字符串。 */
static char *sanitize_log(char *raw, long size) {
	char *clean = malloc((size_t)size + 1);
	if (clean == NULL) {
		fprintf(stderr, "malloc failed\n");
		return NULL;
	}
	long w = 0;
	for (long r = 0; r < size; r++) {
		unsigned char ch = (unsigned char)raw[r];
		/* 跳过回车符, 消除 Windows 换行 (\r\n) 的影响 */
		if (ch == '\r') {
			continue;
		}
		/* 空字符不可打印, 替换为 '?' */
		if (ch == '\0') {
			clean[w++] = '?';
			continue;
		}
		clean[w++] = (char)ch;
	}
	clean[w] = '\0';
	return clean;
}

/* 程序入口: 验证 QEMU 输出日志是否包含预期的内核启动与调度信息。
   用法: validate_output <qemu-log>
   返回值: 0 = 通过, 1 = 验证失败, 2 = 使用方式错误或 I/O 错误 */
int main(int argc, char **argv) {
	if (argc != 2) {
		fprintf(stderr, "usage: %s <qemu-log>\n", argv[0]);
		return 2;
	}

	/* 读取日志文件全部内容 */
	long size = 0;
	char *raw = read_all(argv[1], &size);
	if (raw == NULL) {
		return 2;
	}
	/* 清洗日志: 去除回车和空字符 */
	char *log = sanitize_log(raw, size);
	free(raw);
	if (log == NULL) {
		return 2;
	}

	/* 必须按顺序出现的关键输出行 */
	const char *required[] = {
		"MOS/RISC-V bootstrap",
		"vm self-test passed",
		"satp enabled, kernel still alive",
		"env created: id=0x800 pri=1 name=user-a entry=0x400000",
		"env created: id=0x1001 pri=1 name=user-b entry=0x400000",
		"entering first user env via scheduler...",
		"schedule -> env=0x800 runs=1 name=user-a",
		"A",
		"schedule -> env=0x1001 runs=1 name=user-b",
		"B",
		"timer interrupt #1",
	};

	/* 检查上述关键行是否按序出现 */
	if (!require_in_order(log, required, sizeof(required) / sizeof(required[0]))) {
		free(log);
		return 1;
	}

	/* 检查用户进程输出是否被调度器反复执行 (至少各出现 2 次) */
	int a_count = count_char(log, 'A');
	int b_count = count_char(log, 'B');
	if (a_count < 2 || b_count < 2) {
		fprintf(stderr, "expected repeated user output, got A=%d B=%d\n", a_count, b_count);
		free(log);
		return 1;
	}

	printf("PASS: validated %ld bytes of QEMU output (A=%d, B=%d)\n", size, a_count, b_count);
	free(log);
	return 0;
}