#include "app.h"
#include <mmsystem.h>
#include <opus_multistream.h>
struct AudioState {
 OpusMSDecoder*decoder=nullptr;HWAVEOUT output=nullptr;CRITICAL_SECTION lock;
 WAVEHDR headers[12]={};short*pcm[12]={};bool busy[12]={};int capacity=0,channels=0,rate=0,max_frames=0;
 volatile LONG stopping=0;UINT64 packets=0,dropped=0,total_samples=0,decode_us=0,write_us=0,opened_us=0;UINT max_write_us=0,max_decode_us=0;bool opened=false,muted=false;
 AudioState(){InitializeCriticalSection(&lock);}
 ~AudioState(){DeleteCriticalSection(&lock);}
 bool open(const OPUS_MULTISTREAM_CONFIGURATION*c){
  if(c->channelCount!=2||c->sampleRate!=48000||c->samplesPerFrame<=0||c->samplesPerFrame>5760)return false;
  channels=c->channelCount;rate=c->sampleRate;max_frames=5760;capacity=max_frames*channels*(int)sizeof(short);int error=0;
  decoder=opus_multistream_decoder_create(rate,channels,c->streams,c->coupledStreams,c->mapping,&error);
  if(!decoder||error!=OPUS_OK){app_log("Opus init failed: %d",error);return false;}
  WAVEFORMATEX f={};f.wFormatTag=WAVE_FORMAT_PCM;f.nChannels=(WORD)channels;f.nSamplesPerSec=rate;f.wBitsPerSample=16;f.nBlockAlign=(WORD)(channels*2);f.nAvgBytesPerSec=f.nSamplesPerSec*f.nBlockAlign;
  MMRESULT rc=waveOutOpen(&output,WAVE_MAPPER,&f,0,0,CALLBACK_NULL);if(rc){app_log("waveOutOpen failed: %u",rc);return false;}
  for(int i=0;i<12;i++){pcm[i]=(short*)malloc(capacity);if(!pcm[i])return false;headers[i].lpData=(LPSTR)pcm[i];headers[i].dwBufferLength=capacity;
   rc=waveOutPrepareHeader(output,&headers[i],sizeof(WAVEHDR));if(rc){app_log("waveOutPrepareHeader failed: %u",rc);return false;}busy[i]=false;}
  stopping=0;packets=dropped=total_samples=decode_us=write_us=0;max_write_us=max_decode_us=0;opened_us=clock_us();opened=true;app_log("Audio initialized: Opus %dHz stereo, %d samples per packet, PCM16 waveOut",rate,c->samplesPerFrame);return true;
 }
 void play(const char*data,int size){
  if(stopping||!decoder)return;uint64_t begin=clock_us();short decoded[5760*2];int frames=opus_multistream_decode(decoder,(const unsigned char*)data,size,decoded,max_frames,0);UINT elapsed=(UINT)(clock_us()-begin);decode_us+=elapsed;if(elapsed>max_decode_us)max_decode_us=elapsed;
  if(frames<0){app_log("Opus decode failed: %d",frames);return;}packets++;total_samples+=(UINT)frames;if(muted)return;
  EnterCriticalSection(&lock);if(stopping){LeaveCriticalSection(&lock);return;}
  int available=-1,queued_frames=0;for(int i=0;i<12;i++){if(busy[i]&&(headers[i].dwFlags&WHDR_DONE))busy[i]=false;if(busy[i])queued_frames+=(int)headers[i].dwBufferLength/(channels*2);else if(available<0)available=i;}
  // Accommodate normal WASAPI/network batching, with an explicit 60 ms bound.
  // Still decode dropped packets so the Opus prediction state stays correct.
  int budget=rate*60/1000;if(frames>budget)budget=frames;
  if(available>=0&&queued_frames+frames<=budget){int bytes=frames*channels*2;memcpy(pcm[available],decoded,bytes);headers[available].dwBufferLength=(DWORD)bytes;
   uint64_t write_start=clock_us();MMRESULT rc=waveOutWrite(output,&headers[available],sizeof(WAVEHDR));UINT elapsed=(UINT)(clock_us()-write_start);write_us+=elapsed;if(elapsed>max_write_us)max_write_us=elapsed;
   if(!rc)busy[available]=true;else{dropped++;app_log("waveOutWrite failed: %u",rc);}
  }else dropped++;
  LeaveCriticalSection(&lock);
 }
 void stop(){InterlockedExchange(&stopping,1);EnterCriticalSection(&lock);if(output)waveOutReset(output);LeaveCriticalSection(&lock);}
 void close(){
  stop();if(output){for(int i=0;i<12;i++)if(headers[i].dwFlags&WHDR_PREPARED)waveOutUnprepareHeader(output,&headers[i],sizeof(WAVEHDR));waveOutClose(output);output=nullptr;}
  for(int i=0;i<12;i++){free(pcm[i]);pcm[i]=nullptr;memset(&headers[i],0,sizeof(WAVEHDR));busy[i]=false;}
  if(decoder){opus_multistream_decoder_destroy(decoder);decoder=nullptr;}
  if(opened){app_log("Audio totals: packets=%u dropped=%u PCM_frames=%u wall_ms=%u mean_decode_us=%u max_decode_us=%u mean_wave_write_us=%u max_wave_write_us=%u",(unsigned)packets,(unsigned)dropped,(unsigned)total_samples,(unsigned)((clock_us()-opened_us)/1000),packets?(unsigned)(decode_us/packets):0,max_decode_us,packets?(unsigned)(write_us/packets):0,max_write_us);opened=false;}
 }
};
static AudioState*active_audio=nullptr;
static int init_audio(int,const POPUS_MULTISTREAM_CONFIGURATION c,void*context,int){active_audio=(AudioState*)context;if(!active_audio)return -1;if(!active_audio->open(c)){active_audio->close();return -1;}return 0;}
static void start_audio(){}static void stop_audio(){if(active_audio)active_audio->stop();}static void cleanup_audio(){if(active_audio)active_audio->close();}
static void decode_audio(char*data,int n){if(active_audio&&n>0&&n<65536)active_audio->play(data,n);}
AudioState*audio_create(){return new AudioState;}void audio_destroy(AudioState*s){if(!s)return;s->close();if(active_audio==s)active_audio=nullptr;delete s;}
void audio_callbacks(AudioState*,AUDIO_RENDERER_CALLBACKS*cb){LiInitializeAudioCallbacks(cb);cb->init=init_audio;cb->start=start_audio;cb->stop=stop_audio;cb->cleanup=cleanup_audio;cb->decodeAndPlaySample=decode_audio;cb->capabilities=CAPABILITY_SUPPORTS_ARBITRARY_AUDIO_DURATION;}
bool audio_has_packets(AudioState*s){return s&&s->packets>0;}
void audio_muted(AudioState*s,bool value){if(s)s->muted=value;}
bool audio_selftest(){
 unsigned char mapping[2]={0,1};int error=0;OpusMSEncoder*encoder=opus_multistream_encoder_create(48000,2,1,1,mapping,OPUS_APPLICATION_RESTRICTED_LOWDELAY,&error);if(!encoder||error)return false;
 OpusMSDecoder*decoder=opus_multistream_decoder_create(48000,2,1,1,mapping,&error);if(!decoder||error){opus_multistream_encoder_destroy(encoder);return false;}
 opus_int16 input[480*2],output[5760*2];unsigned char encoded[4000];int packets=0;long long energy=0;
 for(int frame=0;frame<30;frame++){
  for(int i=0;i<480;i++){short sample=((frame*480+i)%110)<55?1000:-1000;input[2*i]=input[2*i+1]=sample;}
  int bytes=opus_multistream_encode(encoder,input,480,encoded,sizeof(encoded));int samples=bytes>0?opus_multistream_decode(decoder,encoded,bytes,output,5760,0):-1;
  if(samples!=480)break;for(int i=0;i<samples*2;i++)energy+=(long long)output[i]*output[i];packets++;
 }
 opus_multistream_decoder_destroy(decoder);opus_multistream_encoder_destroy(encoder);bool ok=packets==30&&energy>0;app_log("Opus codec roundtrip selftest=%s packets=%d (not a speaker playback test)",ok?"PASS":"FAIL",packets);return ok;
}
