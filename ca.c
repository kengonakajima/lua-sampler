#include <AudioToolbox/AudioToolbox.h>
#include <AudioToolbox/AudioServices.h>

#include <unistd.h>

typedef struct {
    int active;
    double lastCallbackAt;
    double startAt;
    int callbackCnt;
} Recorder;

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
            assert( result == 0 ); // couldn't get queue's maximum output packet size
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
double gettime()
{
    struct timeval tmv;
    gettimeofday( &tmv, NULL );
    double t = (tmv.tv_sec * 1000000 + tmv.tv_usec ) / 1000000.0;
    return t;
}
static void callback( void *inUserData,
                      AudioQueueRef inAQ,
                      AudioQueueBufferRef inBuffer,
                      const AudioTimeStamp *inStartTime,
                      UInt32 inNumPackets,
                      const AudioStreamPacketDescription *inPacketDesc )
{
    
    Recorder *recorder = (Recorder*) inUserData;
    recorder->callbackCnt ++;
    double t = gettime();
    double dt = t - recorder->lastCallbackAt;
    double avg = (t - recorder->startAt ) / recorder->callbackCnt;
    recorder->lastCallbackAt = t;
    //    fprintf(stderr, "callback. nPkt:%d active:%d buf:%p sz:%d time:%f avgt:%f\n",
    //            inNumPackets, recorder->active, inBuffer->mAudioData, inBuffer->mAudioDataByteSize, dt, avg  );
    int i;
    unsigned short maxval=0;
    for( i=0;i<inNumPackets;i++) {
        short *s = (short*) inBuffer->mAudioData;
        short val = ntohs(s[0]);
        if(val<0)val *= -1;
        if(val>maxval)maxval=val;
    }
    fprintf(stderr, "%6d ", maxval); // show level meter
    for( i=0;i<maxval/100;i++){
        fprintf(stderr, ".");
    }
    fprintf(stderr,"\n");
    
    if( recorder->active ){
        OSStatus result = AudioQueueEnqueueBuffer( inAQ, inBuffer, 0, NULL );
    }
}
int main( int argc, char **argv  ) {
    AudioStreamBasicDescription recFmt;

    memset( &recFmt, 0, sizeof(recFmt));
    MyGetDefaultInputDeviceSampleRate( &recFmt.mSampleRate );
    fprintf(stderr, "Default sample rate: %f\n", recFmt.mSampleRate );
    recFmt.mChannelsPerFrame = 1; // 2 for stereo
    recFmt.mFormatID = kAudioFormatLinearPCM;
    recFmt.mFormatFlags = kLinearPCMFormatFlagIsSignedInteger | kLinearPCMFormatFlagIsPacked;
    recFmt.mBitsPerChannel = 16;
    recFmt.mFormatFlags |= kLinearPCMFormatFlagIsBigEndian; // to be apparent
    recFmt.mBytesPerPacket = recFmt.mBytesPerFrame = ( recFmt.mBitsPerChannel / 8) * recFmt.mChannelsPerFrame;
    recFmt.mFramesPerPacket = 1;
    recFmt.mReserved = 0;

    Recorder *recorder = (Recorder*)malloc( sizeof(Recorder) );
    recorder->active = 1;
    recorder->lastCallbackAt = 0;
    recorder->startAt = gettime();
    recorder->callbackCnt = 0;

    AudioQueueRef queue;
    OSStatus result = AudioQueueNewInput( &recFmt, callback,
                                          recorder, /*userdata*/
#if 1                                          
                                          NULL, /*run loop */
                                          NULL, /*run loop mode */
#else

                                          CFRunLoopGetCurrent(),
                                          kCFRunLoopCommonModes,
                                          
#endif                                          
                                          0, /*flags */
                                          &queue );
                                 
    fprintf(stderr, "AudioQueueNewInput result: %d\n", result );
    assert(result==0);

    // allocate and enqueue buffers

    int bufferByteSize = MyComputeRecordBufferSize( &recFmt, queue, 0.1 );
    fprintf( stderr, "MyComputeRecordBufferSize: %d bytes\n", bufferByteSize );

    int i;
    for(i=0;i<5;++i){
        AudioQueueBufferRef buffer;
        result = AudioQueueAllocateBuffer( queue, bufferByteSize, &buffer );
        assert(result==0);
        result = AudioQueueEnqueueBuffer( queue, buffer, 0, NULL );
        assert(result==0);
    }

    result = AudioQueueStart(queue, NULL );
    assert(result==0);

    getchar();

    
    return 0;
}
