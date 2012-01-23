#include <AudioToolbox/AudioToolbox.h>
#include <AudioToolbox/AudioServices.h>


#include "lua.h"
#include "lauxlib.h"

#include <strings.h>


// copied from apple's aqrecord sample
// @out number like 44100
OSStatus	MyGetDefaultInputDeviceSampleRate(Float64 *outSampleRate)
{
	OSStatus err;
	AudioDeviceID deviceID = 0;

	// get the default input device
	AudioObjectPropertyAddress addr;
	UInt32 size;
	addr.mSelector = kAudioHardwarePropertyDefaultInputDevice;
	addr.mScope = kAudioObjectPropertyScopeGlobal;
	addr.mElement = 0;
	size = sizeof(AudioDeviceID);
	err = AudioHardwareServiceGetPropertyData(kAudioObjectSystemObject, &addr, 0, NULL, &size, &deviceID);
	if (err) return err;

	// get its sample rate
	addr.mSelector = kAudioDevicePropertyNominalSampleRate;
	addr.mScope = kAudioObjectPropertyScopeGlobal;
	addr.mElement = 0;
	size = sizeof(Float64);
	err = AudioHardwareServiceGetPropertyData(deviceID, &addr, 0, NULL, &size, outSampleRate);

	return err;
}


// Determine the size, in bytes, of a buffer necessary to represent the supplied number
// of seconds of audio data.
static int MyComputeRecordBufferSize(const AudioStreamBasicDescription *format, AudioQueueRef queue, float seconds)
{
	int packets, frames, bytes;
	
	frames = (int)ceil(seconds * format->mSampleRate);
	
	if (format->mBytesPerFrame > 0)
		bytes = frames * format->mBytesPerFrame;
	else {
		UInt32 maxPacketSize;
		if (format->mBytesPerPacket > 0)
			maxPacketSize = format->mBytesPerPacket;	// constant packet size
		else {
			UInt32 propertySize = sizeof(maxPacketSize); 
			OSStatus result = AudioQueueGetProperty(queue, kAudioConverterPropertyMaximumOutputPacketSize, &maxPacketSize,
                                                    &propertySize );
            if(result){
                return -1; // couldn't get queue's maximum output packet size
            }
		}
		if (format->mFramesPerPacket > 0)
			packets = frames / format->mFramesPerPacket;
		else
			packets = frames;	// worst-case scenario: 1 frame in a packet
		if (packets == 0)		// sanity check
			packets = 1;
		bytes = packets * maxPacketSize;
	}
	return bytes;
}


#define RECORDER_BUF_NUM 5
typedef struct {
    lua_State *L;
    AudioStreamBasicDescription recFmt;
    AudioQueueRef queue;
    int active;
    pthread_mutex_t mutex;
    short *data[RECORDER_BUF_NUM];
    size_t dataSizeMaxBytes;
    size_t dataSizeReadBytes[RECORDER_BUF_NUM];
    int currentWriteIndex;
    int currentReadIndex;
} Recorder;



static void globalCallback( void *inUserData,
                            AudioQueueRef inAQ,
                            AudioQueueBufferRef inBuffer,
                            const AudioTimeStamp *inStartTime,
                            UInt32 inNumPackets,
                            const AudioStreamPacketDescription *inPacketDesc )
{
    
    Recorder *recorder = (Recorder*) inUserData;
    pthread_mutex_lock( & recorder->mutex );    
    fprintf(stderr, "callback. nPkt:%d active:%d buf:%p sz:%d curWI:%d\n",
            inNumPackets, recorder->active, inBuffer->mAudioData, inBuffer->mAudioDataByteSize, recorder->currentWriteIndex  );
    
    if( recorder->active ){
        OSStatus result = AudioQueueEnqueueBuffer( inAQ, inBuffer, 0, NULL );
    }

    short *outbuf = recorder->data[ recorder->currentWriteIndex];
    short *inbuf = (short*) inBuffer->mAudioData;
    int i;
    for(i=0;i<inNumPackets;i++){
        outbuf[i] = inbuf[i];
    }
    recorder->dataSizeReadBytes[recorder->currentWriteIndex] = inBuffer->mAudioDataByteSize;
    
    recorder->currentWriteIndex ++;
    if( recorder->currentWriteIndex >= RECORDER_BUF_NUM ){
        recorder->currentWriteIndex = 0;
    }
    pthread_mutex_unlock( & recorder->mutex );        
}


/*
  @in Recorder self
  @in number nChan
  @in number freq
 */
static int luasampler_createRecorder( lua_State *L )
{
    int nChannels = luaL_checkinteger(L,1);
    int nFreq = luaL_checkinteger(L,2);
    void *ptr = lua_newuserdata(L, sizeof(Recorder));
    
    fprintf(stderr, "luasampler_createRecorder. L:%p ptr:%p nch:%d freq:%d\n",L,ptr, nChannels, nFreq );

    Recorder *recorder = (Recorder*) ptr;
    memset( recorder, 0, sizeof(Recorder));
    recorder->L = L;
    recorder->active = 0;
    pthread_mutex_init( & recorder->mutex, NULL );

    pthread_mutex_lock( & recorder->mutex );    
    memset( &recorder->recFmt, 0, sizeof(recorder->recFmt));
    MyGetDefaultInputDeviceSampleRate( &recorder->recFmt.mSampleRate );
    fprintf(stderr, "Default sample rate: %f\n", recorder->recFmt.mSampleRate );
    recorder->recFmt.mChannelsPerFrame = 1; // 2 for stereo
    recorder->recFmt.mFormatID = kAudioFormatLinearPCM;
    recorder->recFmt.mFormatFlags = kLinearPCMFormatFlagIsSignedInteger | kLinearPCMFormatFlagIsPacked;
    recorder->recFmt.mBitsPerChannel = 16;
    recorder->recFmt.mFormatFlags |= kLinearPCMFormatFlagIsBigEndian; // to be apparent
    recorder->recFmt.mBytesPerPacket = recorder->recFmt.mBytesPerFrame = ( recorder->recFmt.mBitsPerChannel / 8) * recorder->recFmt.mChannelsPerFrame;
    recorder->recFmt.mFramesPerPacket = 1;
    recorder->recFmt.mReserved = 0;    

    luaL_getmetatable(L, "sampler" );
    lua_setmetatable(L, -2 );
    pthread_mutex_unlock( & recorder->mutex );        
    return 1;
}

static int luasampler_recorder_prepareBuffer(lua_State *L)
{
    Recorder* recorder = (Recorder*) luaL_checkudata(L,1,"sampler");

    pthread_mutex_lock( & recorder->mutex );

                        
    double sec = luaL_checknumber( L, 2 );
    fprintf(stderr, "luasampler_recorder_prepareBuffer called. num:%f\n", sec );


    OSStatus result = AudioQueueNewInput( &recorder->recFmt, globalCallback,
                                          recorder, /*userdata*/
                                          NULL, /*run loop */
                                          NULL, /*run loop mode */
                                          0, /*flags */
                                          &recorder->queue );
    if(result){
        lua_pushstring( recorder->L, "AudioQueueNewInput failed");
        lua_error(recorder->L);
        pthread_mutex_unlock( & recorder->mutex );                
        return 0;
    }
    
    int bufsize = MyComputeRecordBufferSize( &recorder->recFmt, recorder->queue, sec );
    if( bufsize < 0 ){
        lua_pushstring(L, "invalid arg?");
        lua_error(L);
        pthread_mutex_unlock( & recorder->mutex );        
        return 0;
    }

    recorder->dataSizeMaxBytes = bufsize;
    
    fprintf(stderr, "bufsize:%d\n", bufsize );
    int i;
    for(i=0;i<RECORDER_BUF_NUM;++i){
        recorder->data[i] = (short*) malloc( bufsize );        
    
        AudioQueueBufferRef buffer;
        OSStatus result = AudioQueueAllocateBuffer( recorder->queue, bufsize, &buffer );
        if(result){
            lua_pushstring(L,"AudioQueueAllocateBuffer failed");
            lua_error(L);
            pthread_mutex_unlock( & recorder->mutex );                    
            return 0;
        }
        result = AudioQueueEnqueueBuffer( recorder->queue, buffer, 0, NULL );
        if(result){
            lua_pushstring(L,"AudioQueueEnqueueBuffer failed");
            lua_error(L);
            pthread_mutex_unlock( & recorder->mutex );
            return 0;
        }
    }
    fprintf(stderr, "malloc done\n");
    pthread_mutex_unlock( & recorder->mutex );
    return 0;
}
static int luasampler_recorder_readBuffer( lua_State* L)
{
	Recorder* recorder = (Recorder*)luaL_checkudata(L, 1, "sampler" );
    //    fprintf(stderr, "luasampler_recorder_readBuffer called\n" );
    pthread_mutex_lock( & recorder->mutex );
    
    short *data = recorder->data[ recorder->currentReadIndex ];
    int datanum = recorder->dataSizeReadBytes[ recorder->currentReadIndex ] / sizeof(short);

    if(datanum == 0 ){
        lua_pushnil(L);
        pthread_mutex_unlock( & recorder->mutex );    
        return 1;
    }
    datanum = 10;

    fprintf(stderr,"KKKKKKKKK:%d\n",datanum);
    
    lua_createtable(L,datanum,0);
    int i;
    for(i=0;i< datanum;i++){
        double val = (double)(data[i]) / 32768.0;
        lua_pushnumber(L,val);
        lua_rawseti(L,-2,i+1);
    }

    recorder->dataSizeReadBytes[ recorder->currentReadIndex ] = 0; // set 0 after read
            

    recorder->currentReadIndex ++;
    if( recorder->currentReadIndex >= RECORDER_BUF_NUM ){
        recorder->currentReadIndex = 0;
    }
    fprintf(stderr,"zzzz\n");

    pthread_mutex_unlock( & recorder->mutex );    
    return 1;
}

static int luasampler_recorder_start(lua_State* L)
{
    fprintf(stderr, "luasampler_start called\n" );
	Recorder* recorder = (Recorder*)luaL_checkudata(L, 1, "sampler" );
    pthread_mutex_lock( & recorder->mutex );    

#if 0    
    OSStatus result = AudioQueueStart( recorder->queue, NULL );
    if(result){
        lua_pushstring( L, "AudioQueueStart failed" );
        lua_error(L);
        pthread_mutex_unlock( & recorder->mutex );            
        return 0;
    }
#endif    
    recorder->active = 1;
    pthread_mutex_unlock( & recorder->mutex );
    return 0;
}
static int luasampler_recorder_stop(lua_State* L)
{
    fprintf(stderr, "luasampler_stop called\n" );    
}
static int luasampler_recorder_gc(lua_State* L)
{
    fprintf(stderr, "luasampler_gc called\n" );
    
	Recorder* recorder = (Recorder*)luaL_checkudata(L, 1, "sampler" );
	if(recorder){
        pthread_mutex_lock( & recorder->mutex );        
        // cleanup
        int i;
        for(i=0;i<RECORDER_BUF_NUM;i++){
            if(recorder->data[i]) free(recorder->data[i]);
        }
        pthread_mutex_unlock( & recorder->mutex );                
	}
	return 0;
}

static int luasampler_debug(lua_State* L)
{
    int n=0;
    lua_createtable(L,n,0);
    
    int i;
    for(i=0;i< n;i++){
        double val = rand() / 32768.0;
        lua_pushnumber(L,val);
        assert( lua_type(L,-2) == LUA_TTABLE );
        lua_rawseti(L,-2,i+1);
    }
    
    return 1;
}

static const struct luaL_reg luasampler_recorder_meths[] = {
	{"start", luasampler_recorder_start},
	{"stop", luasampler_recorder_stop},
    {"prepareBuffer", luasampler_recorder_prepareBuffer},
    {"readBuffer", luasampler_recorder_readBuffer},
	{"__gc", luasampler_recorder_gc},
	{0, 0}
};


static const luaL_reg luasampler_funcs[] = {
    {"createRecorder",luasampler_createRecorder },
    {"debug", luasampler_debug},
    {NULL,NULL}
};

LUALIB_API int luaopen_luasampler ( lua_State *L ) {

	luaL_newmetatable(L, "sampler" );
	lua_pushliteral(L, "__index");
	lua_pushvalue(L, -2);
	lua_rawset(L, -3);
    luaL_register(L, 0, luasampler_recorder_meths );

    lua_newtable(L);
    luaL_register(L,NULL, luasampler_funcs );
    return 1;
}
