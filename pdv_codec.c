#include <unistd.h>
#include <sys/wait.h>
#include <signal.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <assert.h>
#include <stdbool.h>
#include <errno.h>
#include <stdint.h>
#include <sys/prctl.h>


#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"

#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "stb_image_write.h"

#define ERROR(args...) {fprintf(stderr, args); exit(1);}


#define MAX(x, y) ((x)>(y)?(x):(y))
#define MIN(x, y) ((x)<(y)?(x):(y))

#define PTRCAST(T, val) *(T*)&(val)

typedef uint64_t u64;
typedef uint32_t u32;
typedef uint16_t u16;
typedef uint8_t u8;
typedef int64_t s64;
typedef int32_t s32;
typedef int16_t s16;
typedef int8_t s8;

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

u32 rgba_to_argb(u32 x) {
	return ((x << 24) & 0xff000000) | ((x << 8) & 0x00ff0000) | ((x >> 8) & 0x0000ff00) | ((x >> 24) & 0x000000ff);
}

#define MAX_MASKS 16
typedef struct {
	u32 masks[MAX_MASKS];
	u64 patterns[MAX_MASKS];

	int count; 
} Masks_Set;

void write_entire_file(char* filename, String str) {
	FILE *fd = fopen(filename,"wb");
	fwrite(str.data, str.count, 1, fd);
	fclose(fd);
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

		// prctl(PR_SET_PDEATHSIG, SIGTERM);

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
	if(result.count == 1 && result.data[0] == '-') {
		ERROR("python script raised an exception (probably zlib is not installed)");
	}
	int status;
	waitpid(pid, &status, 0);
	close(inpipefd[0]);

	return result;
}

#define FRAME_WIDTH 400
#define FRAME_HEIGHT 240
#define PACKED_FRAME_SIZE (FRAME_WIDTH * FRAME_HEIGHT / 8)

String prepare_frame(char* filename, Masks_Set masks_set) {
	u32 W, H, channels;
	u32 *img = (u32*)stbi_load(filename, &W, &H, &channels, 4);
	if(img == NULL) ERROR("can't open file: %s\n", filename);

	if(W != FRAME_WIDTH || H != FRAME_HEIGHT) {
		ERROR("frame has invalid dimensions: %dx%d\n", W, H);
	}

	u8 *packed_image = (u8*)malloc(PACKED_FRAME_SIZE);
	memset(packed_image, 0, PACKED_FRAME_SIZE);

	for(int y = 0, i = 0; y < H; y += 1) {
		for(int x = 0; x < W; x += 1, i += 1) {
			u32 pixel = img[i];
			for(int m = 0; m < masks_set.count; m++) {
				if(pixel == masks_set.masks[m]) {
					u8 p = (masks_set.patterns[m] >> ((y & 3) << 3)) & 0xff;
					packed_image[i / 8] |= (128 >> (x & 7)) & p;
					break;
				}
			}
		}
	}
	stbi_image_free(img);

	return (String){packed_image, PACKED_FRAME_SIZE};
}

void process_image(char* dstname, char* srcname, Masks_Set masks_set) {
	u32 W, H, channels;
	u32 *img = (u32*)stbi_load(srcname, &W, &H, &channels, 4);
	if(img == NULL) ERROR("can't open file: %s\n", srcname);

	for(int y = 0, i = 0; y < H; y += 1) {
		for(int x = 0; x < W; x += 1, i += 1) {
			u32 pixel = img[i];
			img[i] = (pixel & 0xff000000) ? 0xff000000 : 0x00000000;
			for(int m = 0; m < masks_set.count; m++) {
				if(pixel == masks_set.masks[m]) {
					u8 p = (masks_set.patterns[m] >> ((y & 7) << 3));
					img[i] = ((128 >> (x & 7)) & p) ? 0xffffffff : 0xff000000;
					break;
				}
			}
		}
	}
	int r = stbi_write_png(dstname, W, H, 4, img, W * 4);
	if(!r) {
		ERROR("unable to save file: %s", dstname);
	}

	stbi_image_free(img);
}

#define APPEND_PLAIN_DATA(dst, value) append_data(dst, (void*)&value, sizeof(value))
#define MAGIC_PDV "Playdate VID"
#define MAGIC_PDI "Playdate IMG"

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
	u16 begin_index = 1;
	char *filename_fmt = "okgowtf/frame_%04d.png";
	char *result_filename = "result.pdv";
	u16 frames_count = 0;
	bool print_output = true;
	bool make_file = true;
	bool just_image = false;
	Masks_Set masks_set = {{0xffffffff}, {0xffffffffffffffff}, 0};

	for(int i = 1; i < argc; i++) {
		char* arg = argv[i];

		PARAMETER_BLOCK("b",
			if(sscanf(next, "%hu", &begin_index) != 1) {
				ERROR("can't parse begin index: %s", next);
			}
		);
		PARAMETER_BLOCK("n",
			if(sscanf(next, "%hu", &frames_count) != 1) {
				ERROR("can't parse frames count: %s", next);
			}
		);
		PARAMETER_BLOCK("f",
			filename_fmt = next
		);
		PARAMETER_BLOCK("m",
			u32 color, p1, p2;
			if(sscanf(next, "%08x:%08x%08x", &color, &p1, &p2) != 3) {
				ERROR("can't parse %s in format xxxxxxxx:xxxxxxxxxxxxxxxx (8x : 16x)\n", next);
			}
			if(masks_set.count == MAX_MASKS) {
				ERROR("too much mask parameters.\n");
			}

			masks_set.masks[masks_set.count] = rgba_to_argb(color);
			u64 p12 = (((u64)p1) << 32) | ((u64)p2);
			masks_set.patterns[masks_set.count] = p12;
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
		PARAMETER_BLOCK_MONO("i",
			just_image = true;
		);

		ERROR("unexpected argument: %s\n", arg);
	}

	if(!masks_set.count) masks_set.count = 1;

	if(just_image) {
		u32 W, H, stride, size;
		process_image(result_filename, filename_fmt, masks_set);
	} else {
		Dynamic_String file_content = {0};
		String* frames = (String*)malloc(sizeof(String) * frames_count);
		u32* offsets = (u32*)malloc(sizeof(u32) * (frames_count + 1));

		for(int i = 0; i < frames_count; i++) {
			char filename[MAX_FILENAME_SIZE];
			int r = snprintf(filename, MAX_FILENAME_SIZE, filename_fmt, i + begin_index);
			if(r >= MAX_FILENAME_SIZE || r <= 0) ERROR("format generated invalid / too large name\n");
			if(print_output && ((i + 1) % 10 == 0 || frames_count <= 20)) printf("preparing frame: %s\n", filename);

			String packed_frame = prepare_frame(filename, masks_set);
			String compressed_frame = compress(packed_frame);
			free_string(packed_frame);

			frames[i] = compressed_frame;
		}
		if(print_output) printf("prepering done\n");

		offsets[0] = 0;
		for(int i = 0; i < frames_count; i++) {
			offsets[i + 1] = offsets[i] + frames[i].count;
		}
		for(int i = 0; i <= frames_count; i++) {
			offsets[i] = (offsets[i] << 2) | (i == frames_count ? 0 : 1);
		}

		{
			u32 pad_u32 = 0;
			u16 pad_u16 = 0;
			float framerate = 15.0;
			u16 width = FRAME_WIDTH;
			u16 height = FRAME_HEIGHT;

			append_c_string(&file_content, MAGIC_PDV);
			APPEND_PLAIN_DATA(&file_content, pad_u32);
			APPEND_PLAIN_DATA(&file_content, frames_count);
			APPEND_PLAIN_DATA(&file_content, pad_u16);
			APPEND_PLAIN_DATA(&file_content, framerate);
			APPEND_PLAIN_DATA(&file_content, width);
			APPEND_PLAIN_DATA(&file_content, height);		
		}
		if(print_output) printf("header done\n");

		append_data(&file_content, (void*)offsets, sizeof(u32) * (frames_count + 1));
		free(offsets);
		if(print_output) printf("offsets done\n");

		for(int i = 0; i < frames_count; i++) {
			append_string(&file_content, frames[i]);
			free_string(frames[i]);
		}
		free(frames);
		if(print_output) printf("frames done\n");

		if(make_file) {
			write_entire_file(result_filename, file_content.view);
		} else {
			write(STDOUT_FILENO, file_content.data, file_content.count);
		}
		free_dynamic_string(file_content);
	}

	return 0;
}