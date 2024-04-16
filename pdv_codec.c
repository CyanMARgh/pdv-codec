#include <unistd.h>
#include <sys/wait.h>
#include <sys/prctl.h>
#include <signal.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <assert.h>
#include <stdbool.h>
#include <errno.h>
#include <stdint.h>

#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"
#define ERROR(args...) {fprintf(stderr, args); exit(1);}


#define MAX(x, y) ((x)>(y)?(x):(y))
#define MIN(x, y) ((x)<(y)?(x):(y))

#define PTRCAST(T, val) *(T*)&(val)

typedef uint64_t u64;
typedef int64_t s64;

typedef struct {
	char* data;
	s64 count;
} String;

String from_c_string(char* str) {
	return (String){str, strlen(str)};
}

void free_string(String str) {
	free(str.data);
}

typedef union {
	String view;
	struct {
		char* data;
		s64 count;
		s64 allocated;
	};
} Dynamic_String;

void check_is_valid(Dynamic_String *str) {
	assert(str->allocated >= str->count);
	assert(str->count >= 0);
}

void resize(Dynamic_String *str, s64 new_allocated) {
	check_is_valid(str);
	assert(new_allocated >= str->allocated);
	char* new_data = (char*)realloc(str->data, new_allocated);
	assert(new_data != NULL);
	str->data = new_data;
	str->allocated = new_allocated;
}

void append_data(Dynamic_String *str, char *data, s64 count) {
	if(str->count + count > str->allocated) {
		resize(str, MAX(str->count + count, str->allocated * 2));
	}
	memcpy(str->data + str->count, data, count);
	str->count += count;
}
void append_string(Dynamic_String *str, String str2) {
	append_data(str, str2.data, str2.count);
}
void append_c_string(Dynamic_String *str, char* cstr) {
	append_data(str, cstr, strlen(cstr));
}

void free_dynamic_string(Dynamic_String str) {
	free_string(str.view);
}
void reset_string(Dynamic_String *str) {
	free_dynamic_string(*str);
	str->data = NULL;
	str->count = 0;
	str->allocated = 0;
}

uint32_t rgba_to_argb(uint32_t x) {
	x = (x << 16) | ((x >> 16) & 0xffff);
	return (x << 8) | ((x >> 8) & 0xff);
}

#define MAX_MASKS 16
typedef struct {
	uint32_t masks[MAX_MASKS];
	uint64_t patterns[MAX_MASKS];

	int count; 
} Masks_Set;

void write_entire_file(char* filename, String str) {
	FILE *fd = fopen(filename,"wb");
	fwrite(str.data, str.count, 1, fd);
	fclose(fd);
}

#define FRAME_WIDTH 400
#define FRAME_HEIGHT 240
#define PACKED_IMAGE_SIZE (FRAME_WIDTH * FRAME_HEIGHT / 8)

String prepare_frame(char* filename, Masks_Set masks_set) {
	uint32_t W, H, channels;
	uint32_t *img = (uint32_t*)stbi_load(filename, &W, &H, &channels, 4);
	if(img == NULL) ERROR("can't open file: %s\n", filename);

	if(W != FRAME_WIDTH || H != FRAME_HEIGHT) {
		ERROR("frame has invalid dimensions: %dx%d\n", W, H);
	}

	uint8_t *packed_image = (uint8_t*)malloc(PACKED_IMAGE_SIZE);
	memset(packed_image, 0, PACKED_IMAGE_SIZE);

	for(int y = 0, i = 0; y < H; y += 1) {
		for(int x = 0; x < W; x += 1, i += 1) {
			uint32_t pixel = img[i];
			for(int m = 0; m < masks_set.count; m++) {
				if(pixel == masks_set.masks[m]) {
					uint8_t p = (masks_set.patterns[m] >> ((y & 3) << 3)) & 0xff;
					packed_image[i / 8] |= (128 >> (x & 7)) & p;
					break;
				}
			}
		}
	}
	stbi_image_free(img);

	return (String){packed_image, PACKED_IMAGE_SIZE};
}

int write_all(s64 fd, String str) {
	while(str.count > 0) {
		int r = write(fd, str.data, str.count);
		if(r == -1) return 1;
		assert(r >= 0);
		str.count -= r;
		str.data += r;
	}
	assert(str.count == 0);
	return 0;
}
String read_all(int fd) {
	Dynamic_String str = {0};
	char buf[256];
	while(1) {
		int r = read(fd, buf, sizeof(buf));
		if(r > 0) {
			assert(r <= 256);
			append_data(&str, buf, r);
		} else if(r == 0) {
			break;
		} else  {
			reset_string(&str);
			break;
		}
	}
	return str.view;
}

String compress(String msg) {
	int inpipefd[2];
	int outpipefd[2];

	pipe(inpipefd);
	pipe(outpipefd);
	pid_t pid = fork();
	if (pid == 0) {
		dup2(outpipefd[0], STDIN_FILENO);
		dup2(inpipefd[1], STDOUT_FILENO);

		prctl(PR_SET_PDEATHSIG, SIGTERM);

		close(outpipefd[1]);
		close(inpipefd[0]);

		execl("./zlib_compress.py", "./zlib_compress.py", (char*) NULL);

		exit(1);
	}

	close(outpipefd[0]);
	close(inpipefd[1]);

	write_all(outpipefd[1], msg);
	close(outpipefd[1]);

	String result = read_all(inpipefd[0]);
	int status;
	waitpid(pid, &status, 0);
	close(inpipefd[0]);

	return result;
}

#define APPEND_PLAIN_DATA(dst, value) append_data(dst, (void*)&value, sizeof(value))
#define MAGIC "Playdate VID"

#define MAX_FILENAME_SIZE 1024

#define PARAMETER_BLOCK(P, ...)\
if(!strcmp(arg, "-" P)) {\
	if(++i == argc) {\
		ERROR("argument after \"-" P "\" not specified");\
	}\
	char* next = argv[i];\
	__VA_ARGS__;\
	continue;\
}

#define PARAMETER_BLOCK_MONO(P, ...)\
if(!strcmp(arg, "-" P)) {\
	__VA_ARGS__;\
	continue;\
}

int main(int argc, char **argv) {
	uint16_t begin_index = 1;
	char *filename_fmt = "okgowtf/frame_%04d.png";
	char *result_filename = "result.pdv";
	uint16_t frames_count = 0;
	bool print_output = true;
	bool make_file = true;

	Masks_Set masks_set = {{0xffffffff}, {0xffffffffffffffff}, 0};

	for(int i = 1; i < argc; i++) {
		char* arg = argv[i];

		PARAMETER_BLOCK("b",
			if(sscanf(next, "%hu", &begin_index) != 1) {
				ERROR("can't parse begin index: %s", next);
			}
			// printf("begin index: %d\n", begin_index);
		);
		PARAMETER_BLOCK("n",
			if(sscanf(next, "%hu", &frames_count) != 1) {
				ERROR("can't parse frames count: %s", next);
			}
			// printf("frames count: %d\n", frames_count);
		);
		PARAMETER_BLOCK("f",
			filename_fmt = next
			// printf("files format: %s\n", filename_fmt);
		);
		PARAMETER_BLOCK("m",
			uint32_t color, p1, p2;
			if(sscanf(next, "%08x:%08x%08x", &color, &p1, &p2) != 3) {
				ERROR("can't parse %s in format xxxxxxxx:xxxxxxxxxxxxxxxx (8x : 16x)\n", next);
			}
			// else {
			// 	printf("color: %08x, pattern: %16lx\n", color, (((uint64_t)p1) << 32) | ((uint32_t)p2));
			// }
			if(masks_set.count == MAX_MASKS) {
				ERROR("too much mask parameters.\n");
			}

			masks_set.masks[masks_set.count] = rgba_to_argb(color);
			masks_set.patterns[masks_set.count] = (((uint64_t)p1) << 32) | ((uint32_t)p2);
			masks_set.count += 1;
		);
		PARAMETER_BLOCK("o",
			result_filename = next;
		);
		PARAMETER_BLOCK_MONO("no",
			print_output = false;
		);
		PARAMETER_BLOCK_MONO("p",
			print_output = false;
			make_file = false;
		);

		ERROR("unexpected argument: %s\n", arg);
	}

	if(!masks_set.count) masks_set.count = 1;

	String* frames = (String*)malloc(sizeof(String) * frames_count);
	uint32_t* offsets = (uint32_t*)malloc(sizeof(uint32_t) * (frames_count + 1));

	for(int i = 0; i < frames_count; i++) {
		char filename[MAX_FILENAME_SIZE];
		int r = snprintf(filename, MAX_FILENAME_SIZE, filename_fmt, i + begin_index);
		if(r >= MAX_FILENAME_SIZE || r <= 0) ERROR("format generated invalid name\n");
		if(print_output && ((i + 1) % 10 == 0 || frames_count <= 20)) printf("preparing frame: %s\n", filename);
		String packed_frame = prepare_frame(filename, masks_set);

		frames[i] = compress(packed_frame);

		free_string(packed_frame);
	}
	if(print_output) printf("prepering done\n");

	offsets[0] = 0;
	for(int i = 0; i < frames_count; i++) {
		offsets[i + 1] = offsets[i] + frames[i].count;
	}
	for(int i = 0; i <= frames_count; i++) {
		offsets[i] = (offsets[i] << 2) | (i == frames_count ? 0 : 1);
	}

	Dynamic_String pdv_file = {0};

	{
		uint32_t pad_u32 = 0;
		uint16_t pad_u16 = 0;
		float framerate = 15.0;
		uint16_t width = FRAME_WIDTH;
		uint16_t height = FRAME_HEIGHT;

		append_c_string(&pdv_file, MAGIC);
		APPEND_PLAIN_DATA(&pdv_file, pad_u32);
		APPEND_PLAIN_DATA(&pdv_file, frames_count);
		APPEND_PLAIN_DATA(&pdv_file, pad_u16);
		APPEND_PLAIN_DATA(&pdv_file, framerate);
		APPEND_PLAIN_DATA(&pdv_file, width);
		APPEND_PLAIN_DATA(&pdv_file, height);		
	}
	if(print_output) printf("header done\n");


	append_data(&pdv_file, (void*)offsets, sizeof(uint32_t) * (frames_count + 1));
	free(offsets);
	if(print_output) printf("offsets done\n");

	for(int i = 0; i < frames_count; i++) {
		append_string(&pdv_file, frames[i]);
		free_string(frames[i]);
	}
	free(frames);
	if(print_output) printf("frames done\n");

	if(make_file) {
		write_entire_file(result_filename, pdv_file.view);
	} else {
		write(STDOUT_FILENO, pdv_file.data, pdv_file.count);
	}
	free_dynamic_string(pdv_file);
	return 0;
}