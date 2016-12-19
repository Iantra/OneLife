#include "minorGems/game/game.h"
#include <math.h>


// overal loudness of music
static double musicLoudness = 0;
static double musicTargetLoudness = 0;

extern double musicHeadroom;


// set so that a full 0-to-1 loudness transition happens 
// in 1 second
static double loudnessChangePerSample;


#include "ogg.h"


static OGGHandle musicOGG = NULL;
static double musicLengthSeconds = -1;

static char musicOGGReady = false;
static char musicOGGPlaying = false;


static unsigned char *oggData = NULL;

static int asyncLoadHandle = -1;


static char musicStarted = false;


static double age = -1;
static double ageRate = -1;
static double ageSetTime = -1;

static double ageNextMusicDone = -1;



static double getCurrentAge() {
    double timePassed = game_getCurrentTime() - ageSetTime;
    
    return age + ageRate * timePassed;
    }



// one per chunk
// the crossFadeSamples samples that occur BEFORE the coresponding chunk




void initMusicPlayer() {
    }



// returns an asynch file read handle, or -1 on failure
static int startNextAgeFileRead( double inAge ) {
    int nextFiveBlock = ceil( inAge / 5 );
    

    // too close to that age transition,
    // start on next
    if( nextFiveBlock * 5 < inAge + 1 ) {
        nextFiveBlock += 1;
        }
    
    ageNextMusicDone = nextFiveBlock * 5;

    char *searchString = autoSprintf( "_%d.ogg", nextFiveBlock );

    File musicDir( NULL, "music" );
    
    int handle = -1;
    
    if( musicDir.exists() && musicDir.isDirectory() ) {
        
        int numChildren;
        File **childFiles = musicDir.getChildFiles( &numChildren );
        
        for( int i=0; i<numChildren; i++ ) {
            
            char *fileName = childFiles[i]->getFileName();
            
            char match = ( strstr( fileName, searchString ) != NULL );
            
            delete [] fileName;
            if( match ) {
                
                char *fullName = childFiles[i]->getFullFileName();
                
                handle = startAsyncFileRead( fullName );
                
                delete [] fullName;
                break;
                }
            }
        for( int i=0; i<numChildren; i++ ) {
            delete childFiles[i];
            }
        delete [] childFiles;
        }
    
    return handle;
    }



void restartMusic( double inAge, double inAgeRate ) {

    // for testing
    //inAge = 0;
    
    age = inAge;
    ageRate = inAgeRate;

    ageSetTime = game_getCurrentTime();

    asyncLoadHandle = startNextAgeFileRead( inAge );
    
    
    musicStarted = true;
    
    // fade in at start of music
    musicLoudness = 0;
    }



void instantStopMusic() {
    musicStarted = false;
    }



void stepMusic() {

    // no lock needed when checking this flag
    if( musicOGGReady ) {
        // already have next one loaded and possibly playing
        return;
        }

    // else our audio callback, running in the audio thread
    // isn't doing anything until we say ready (with a lock around it)

    // we can safely manipulate the shared data now
    
    if( musicOGG != NULL ) {
        closeOGG( musicOGG );
        musicOGG = NULL;
        
        delete [] oggData;
        oggData = NULL;
        
        asyncLoadHandle = startNextAgeFileRead( getCurrentAge() );
        }
    
    
    if( oggData == NULL && asyncLoadHandle != -1 ) {
        
        if( checkAsyncFileReadDone( asyncLoadHandle ) ) {
            

            int oggDataLength;
            oggData = getAsyncFileData( asyncLoadHandle, &oggDataLength );
            
            asyncLoadHandle = -1;
            
            if( oggData != NULL ) {
                
                musicOGG = openOGG( oggData, oggDataLength );

                if( musicOGG != NULL ) {
                    musicLengthSeconds = 
                        (double) getOGGTotalSamples( musicOGG ) / 
                        (double) getSampleRate();

                    // need lock here to prevent operation re-ordering, even
                    // though setting the flag may be atomic
                    // flag being set implies other shared data are in
                    // a correct state
                    lockAudio();
                    musicOGGReady = true;
                    unlockAudio();
                    }
                }
            }
        }
    }



static float *samplesL = NULL;
static float *samplesR = NULL;



void freeMusicPlayer() {
    if( musicOGG != NULL ) {
        closeOGG( musicOGG );
        musicOGG = NULL;
        
        delete [] oggData;
        oggData = NULL;
        }
    
    
    if( samplesL != NULL ) {
        delete [] samplesL;
        samplesL = NULL;
        }
    if( samplesR != NULL ) {
        delete [] samplesR;
        samplesR = NULL;
        }
    }



void hintBufferSize( int inLengthToFillInBytes ) {
    // 2 bytes for each channel of stereo sample
    int numSamples = inLengthToFillInBytes / 4;

    if( samplesL != NULL ) {
        delete [] samplesL;
        samplesL = NULL;
        }
    if( samplesR != NULL ) {
        delete [] samplesR;
        samplesR = NULL;
        }
    

    samplesL = new float[ numSamples ];
    samplesR = new float[ numSamples ];

    for( int i=0; i<numSamples; i++ ) {
        samplesL[i] = 0;
        samplesR[i] = 0;
        }    
    }



// called by platform to get more samples
void getSoundSamples( Uint8 *inBuffer, int inLengthToFillInBytes ) {
    
    if( !musicOGGReady || !musicStarted ) {
        memset( inBuffer, 0, inLengthToFillInBytes );
        
        return;
        }


    if( !musicOGGPlaying ) {
        
        // determine if we should start playing it
        
        double startAge = ageNextMusicDone - musicLengthSeconds * ageRate;
        
        if( startAge < getCurrentAge() ) {
            musicOGGPlaying = true;
            }
        }
    

    if( ! musicOGGPlaying ) {
        // wait until later to start it
        
        memset( inBuffer, 0, inLengthToFillInBytes );
        return;
        }




    // 2 bytes for each channel of stereo sample
    int numSamples = inLengthToFillInBytes / 4;

    if( samplesL == NULL ) {
        // never got hint
        hintBufferSize( inLengthToFillInBytes );
        }


    int numRead = readNextSamplesOGG( musicOGG, numSamples,
                                      samplesL, samplesR );


    if( numRead != numSamples ) {
        // hit end of file
        musicOGGReady = false;
        musicOGGPlaying = false;
        }
    

    // now copy samples into Uint8 buffer
    // while also adjusting loudness of whole mix
    char loudnessChanging = false;
    if( musicLoudness != musicTargetLoudness ) {
        loudnessChanging = true;
        }
    
    int streamPosition = 0;
    for( int i=0; i != numRead; i++ ) {
        samplesL[i] *= musicLoudness * musicHeadroom;
        samplesR[i] *= musicLoudness * musicHeadroom;
    
        if( loudnessChanging ) {
            
            if( musicLoudness < musicTargetLoudness ) {
                musicLoudness += loudnessChangePerSample;
                if( musicLoudness > musicTargetLoudness ) {
                    musicLoudness = musicTargetLoudness;
                    loudnessChanging = false;
                    }
                }
            else if( musicLoudness > musicTargetLoudness ) {
                musicLoudness -= loudnessChangePerSample;
                if( musicLoudness < musicTargetLoudness ) {
                    musicLoudness = musicTargetLoudness;
                    loudnessChanging = false;
                    }
                }
            }
        
        
        
        Sint16 intSampleL = (Sint16)( lrint( 32767 * samplesL[i] ) );
        Sint16 intSampleR = (Sint16)( lrint( 32767 * samplesR[i] ) );
        
        inBuffer[ streamPosition ] = (Uint8)( intSampleL & 0xFF );
        inBuffer[ streamPosition + 1 ] = (Uint8)( ( intSampleL >> 8 ) & 0xFF );
        
        inBuffer[ streamPosition + 2 ] = (Uint8)( intSampleR & 0xFF );
        inBuffer[ streamPosition + 3 ] = (Uint8)( ( intSampleR >> 8 ) & 0xFF );
        
        streamPosition += 4;
        }

    // if we hit end of song before the end of the buffer
    // fill rest with 0
    while( streamPosition < inLengthToFillInBytes ) {
        inBuffer[ streamPosition ] = 0;
        streamPosition++;
        }
    }



// need to synch these with audio thread

void setMusicLoudness( double inLoudness ) {
    lockAudio();
    
    musicTargetLoudness = inLoudness;
    
    unlockAudio();
    }



double getMusicLoudness() {
    return musicLoudness;
    }