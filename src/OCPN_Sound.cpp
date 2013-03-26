#include "OCPN_Sound.h"


#ifdef OCPN_USE_PORTAUDIO

//      Static variables associated with portaudio library

PaStream *stream;
void *sdata;
int sindex;
int smax_samples;
bool splaying;

/* This routine will be called by the PortAudio engine when audio is needed.
 * It may called at interrupt level on some machines so don't do anything
 * that could mess up the system like calling malloc() or free().
 */
static int OCPNSoundCallback( const void *inputBuffer, void *outputBuffer,
                           unsigned long framesPerBuffer,
                           const PaStreamCallbackTimeInfo* timeInfo,
                           PaStreamCallbackFlags statusFlags,
                           void *userData )
{
    /* Cast data passed through stream to our structure. */
    wxUint16 *data = (wxUint16 *)userData;
    wxUint16 *out = (wxUint16*)outputBuffer;
    unsigned int i;
    bool bdone = false;
    (void) inputBuffer; /* Prevent unused variable warning. */
    for( i=0; i<framesPerBuffer; i++ )
    {
        *out = data[sindex];
        out++;
        sindex++;

        if( sindex >= smax_samples ) {
            bdone = true;
            break;
        }
    }

    if(bdone)
        return paComplete;
    else
        return 0;
}

static void OCPNSoundFinishedCallback( void *userData )
{
    splaying = false;
}


OCPN_Sound::OCPN_Sound()
{
    if(!portaudio_initialized) {
        PaError err = Pa_Initialize();
        if( err != paNoError ) {
            printf( "PortAudio CTOR error: %s\n", Pa_GetErrorText( err ) );
        }
        portaudio_initialized = true;
    }

    m_osdata = NULL;
    m_OK = false;
    m_stream = NULL;

}

OCPN_Sound::~OCPN_Sound()
{
    if( m_stream ) {
        Pa_CloseStream( m_stream );
    }

    FreeMem();
}

bool OCPN_Sound::IsOk() const
{
    return m_OK;
}

bool OCPN_Sound::Create(const wxString& fileName, bool isResource)
{
    m_OK = false;

    FreeMem();

    wxFile fileWave;
    if (!fileWave.Open(fileName, wxFile::read))
    {
        return false;
    }

    wxFileOffset lenOrig = fileWave.Length();
    if ( lenOrig == wxInvalidOffset )
        return false;

    size_t len = wx_truncate_cast(size_t, lenOrig);
    wxUint8 *data = new wxUint8[len];
    if ( fileWave.Read(data, len) != lenOrig )
    {
        delete [] data;
        wxLogError(_("Couldn't load sound data from '%s'."), fileName.c_str());
        return false;
    }
    
    if (!LoadWAV(data, len, true))
    {
        delete [] data;
        wxLogError(_("Sound file '%s' is in unsupported format."),
                   fileName.c_str());
        return false;
    }
    
    sdata = m_osdata->m_data;           //The raw sound data
    sindex = 0;
    smax_samples = m_osdata->m_samples;
 
    PaError err;
    m_stream = NULL;
    
    /* Open an audio I/O stream. */
    err = Pa_OpenDefaultStream( &m_stream,
                                0, /* no input channels */
                                m_osdata->m_channels, 
                                paInt16, 
                                m_osdata->m_samplingRate,
                                256, /* frames per buffer, i.e. the number
                                of sample frames that PortAudio will
                                request from the callback. Many apps
                                may want to use
                                paFramesPerBufferUnspecified, which
                                tells PortAudio to pick the best,
                                possibly changing, buffer size.*/
                                OCPNSoundCallback, /* this is your callback function */
                                sdata ); /*This is a pointer that will be passed to
                                your callback*/
    if( err != paNoError )
        printf( "PortAudio Create() error: %s\n", Pa_GetErrorText( err ) );

    err = Pa_SetStreamFinishedCallback( m_stream, OCPNSoundFinishedCallback ); 
    if( err != paNoError )
        printf( "PortAudio SetStreamFinishedCallback() error: %s\n", Pa_GetErrorText( err ) );
    
    m_OK = true;
    
    return true;
}

bool OCPN_Sound::Play(unsigned flags) const
{
    if(m_stream) {
        Pa_StopStream( m_stream );
    
        if( !splaying ) {
            sdata = m_osdata->m_data;           //The raw sound data
            sindex = 0;
            smax_samples = m_osdata->m_samples;
            stream = m_stream;
    
            PaError err = Pa_StartStream( stream );
            if( err != paNoError ) {
                printf( "PortAudio Play() error: %s\n", Pa_GetErrorText( err ) );
                return false;
            }
            else {
                splaying = true;
                return true;
            }
        }
        else
            return false;
    }
    return false;
}

bool OCPN_Sound::IsPlaying() const
{
    return splaying;
}

void OCPN_Sound::Stop()
{
    Pa_StopStream( m_stream );
    splaying = false;
}

typedef struct
{
    wxUint32      uiSize;
    wxUint16      uiFormatTag;
    wxUint16      uiChannels;
    wxUint32      ulSamplesPerSec;
    wxUint32      ulAvgBytesPerSec;
    wxUint16      uiBlockAlign;
    wxUint16      uiBitsPerSample;
} WAVEFORMAT;

#define WAVE_FORMAT_PCM  1
#define WAVE_INDEX       8
#define FMT_INDEX       12

bool OCPN_Sound::LoadWAV(const wxUint8 *data, size_t length, bool copyData)
{
    // the simplest wave file header consists of 44 bytes:
    //
    //      0   "RIFF"
    //      4   file size - 8
    //      8   "WAVE"
    //
    //      12  "fmt "
    //      16  chunk size                  |
    //      20  format tag                  |
    //      22  number of channels          |
    //      24  sample rate                 | WAVEFORMAT
    //      28  average bytes per second    |
    //      32  bytes per frame             |
    //      34  bits per sample             |
    //
    //      36  "data"
    //      40  number of data bytes
    //      44  (wave signal) data
    //
    // so check that we have at least as much
    if ( length < 44 )
        return false;
    
    WAVEFORMAT waveformat;
    memcpy(&waveformat, &data[FMT_INDEX + 4], sizeof(WAVEFORMAT));
    waveformat.uiSize = wxUINT32_SWAP_ON_BE(waveformat.uiSize);
    waveformat.uiFormatTag = wxUINT16_SWAP_ON_BE(waveformat.uiFormatTag);
    waveformat.uiChannels = wxUINT16_SWAP_ON_BE(waveformat.uiChannels);
    waveformat.ulSamplesPerSec = wxUINT32_SWAP_ON_BE(waveformat.ulSamplesPerSec);
    waveformat.ulAvgBytesPerSec = wxUINT32_SWAP_ON_BE(waveformat.ulAvgBytesPerSec);
    waveformat.uiBlockAlign = wxUINT16_SWAP_ON_BE(waveformat.uiBlockAlign);
    waveformat.uiBitsPerSample = wxUINT16_SWAP_ON_BE(waveformat.uiBitsPerSample);
    
    // get the sound data size
    wxUint32 ul;
    memcpy(&ul, &data[FMT_INDEX + waveformat.uiSize + 12], 4);
    ul = wxUINT32_SWAP_ON_BE(ul);
    
    if ( length < ul + FMT_INDEX + waveformat.uiSize + 16 )
        return false;
    
    if (memcmp(data, "RIFF", 4) != 0)
        return false;
    if (memcmp(&data[WAVE_INDEX], "WAVE", 4) != 0)
        return false;
    if (memcmp(&data[FMT_INDEX], "fmt ", 4) != 0)
        return false;
    if (memcmp(&data[FMT_INDEX + waveformat.uiSize + 8], "data", 4) != 0)
        return false;
    
    if (waveformat.uiFormatTag != WAVE_FORMAT_PCM)
        return false;
    
    if (waveformat.ulSamplesPerSec !=
        waveformat.ulAvgBytesPerSec / waveformat.uiBlockAlign)
        return false;
    
    m_osdata = new OCPNSoundData;
    m_osdata->m_dataWithHeader = NULL;
    m_osdata->m_channels = waveformat.uiChannels;
    m_osdata->m_samplingRate = waveformat.ulSamplesPerSec;
    m_osdata->m_bitsPerSample = waveformat.uiBitsPerSample;
    m_osdata->m_samples = ul / (m_osdata->m_channels * m_osdata->m_bitsPerSample / 8);
    m_osdata->m_dataBytes = ul;
    
    if (copyData)
    {
        m_osdata->m_dataWithHeader = new wxUint8[length];
        memcpy(m_osdata->m_dataWithHeader, data, length);
    }
    else
        m_osdata->m_dataWithHeader = (wxUint8*)data;
    
    m_osdata->m_data =
    (&m_osdata->m_dataWithHeader[FMT_INDEX + waveformat.uiSize + 8]);
    
    return true;
}

void OCPN_Sound::FreeMem(void)
{
    if( m_osdata ) {
        if( m_osdata->m_dataWithHeader )
            delete [] m_osdata->m_dataWithHeader;
        delete m_osdata;
        
        m_osdata = NULL;
    }
}

#else  //OCPN_USE_PORTAUDIO
OCPN_Sound::OCPN_Sound()
{
//    wxSound::wxSound();
}

OCPN_Sound::~OCPN_Sound()
{
}

bool OCPN_Sound::IsOk() const
{
    return wxSound::IsOk();
}

bool OCPN_Sound::Create(const wxString& fileName, bool isResource)
{
    return wxSound::Create(fileName, isResource);
}

bool OCPN_Sound::Play(unsigned flags) const
{
    return wxSound::Play(flags);
}

bool OCPN_Sound::IsPlaying() const
{
#ifndef __WXMSW__    
    return wxSound::IsPlaying();
#else
    return false;
#endif    
}

void OCPN_Sound::Stop()
{
    wxSound::Stop();
}

#endif  //OCPN_USE_PORTAUDIO

