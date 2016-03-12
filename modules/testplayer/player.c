
#include <windows.h>
#include <stdio.h>
#include <fcntl.h>
#include <sys/stat.h>

#include <stdint.h>
typedef uint32_t u32;
typedef uint16_t u16;
typedef  int16_t s16;

typedef struct {
	char  ChunkId[4]; // Main Chunk
	u32   ChunkSize;
	char  Format[4];
	char  FormatChunkId[4]; // Format Sub Chunk
	u32   FormatChunkSize;
	u16   AudioFormat;
	u16   NumChannels;
	u32   SampleRate;
	u32   ByteRate;
	u16   BlockAlign;
	u16   BitsPerSample;
	char  DataChunkId[4]; // Data Sub Chunk
	u32   DataChunkSize;
} wav_header_t;

typedef struct {
	u32 idx;
	u32 size;
	wav_header_t *header;
	s16          *samples;
} audio_file_t;

static int open_audio(const char *fn, audio_file_t* audio)
{
	struct stat st;

	FILE *fd = fopen(fn, "r");
	if( fd == NULL ) return -1;

	stat(fn, &st);

	audio->idx     = 0;
	printf("%u\n", st.st_size);
	audio->header  = (wav_header_t*) malloc(st.st_size);

	fread(audio->header, 1, st.st_size, fd);
	printf("%ld\n", audio->header->SampleRate);

	audio->samples = (u16*)(&audio->header->DataChunkId[8]);
	audio->size    = audio->header->DataChunkSize / sizeof(s16);
	printf("%ld\n", audio->header->SampleRate);

	fclose(fd);

	return 0;
}

typedef float f32;
typedef struct {
	u32 inp_rate, out_rate;
	u32 inp, out;
	float a, b;
} resampler_t;
s16 resample_next(resampler_t *r, audio_file_t *a) {
	while( r->inp > r->out ) { // While More Data Needed
		r->inp -= r->out; r->out = r->out_rate;
		r->a = r->b;
		r->b = ((float)(a->samples[ a->idx = (a->idx + 1) % a->size ])) / (u32)((1 << 16) - 1);
	}
	r->out -= r->inp; r->inp = r->inp_rate;
	{
		f32 pos = ( (f32)(r->inp) / (f32)(r->inp_rate) );
		f32 nxt = (r->a * pos) + (r->b *(1.f-pos));
		return (s16)(nxt * (u32)((1 << 16) - 1));
	};
}

static int play_audio(audio_file_t *audio, s16 *samples, size_t count, size_t freq)
{
	static resampler_t rr = { 22050, 0, 22050, 0, 0.f, 0.f };
	const s16* end = samples + count;

	if( rr.out_rate == 0 ) rr.out = rr.out_rate = freq;
	for(; samples < end; samples++) {
		*samples = resample_next(&rr, audio);
		//*samples = audio->samples[ audio->idx = (audio->idx + 1) % audio->size ];
	}
	return 0;
}

int main_player()
{
	audio_file_t audio;
   	open_audio("background.wav", &audio);

	printf("%ld\n", (audio.header)->SampleRate);
	printf("hello world %ld \n", (audio.header)->SampleRate);

	return 0;
}
