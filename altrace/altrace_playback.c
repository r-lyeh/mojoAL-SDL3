/**
 * MojoAL; a simple drop-in OpenAL implementation.
 *
 * Please see the file LICENSE.txt in the source's root directory.
 *
 *  This file written by Ryan C. Gordon.
 */

#define APPNAME "altrace_playback"
#include "altrace_common.h"

static int dump_log = 1;
static int run_log = 0;

static void quit_altrace_playback(void);

#define MAX_IOBLOBS 32
static uint8 *ioblobs[MAX_IOBLOBS];
static size_t ioblobs_len[MAX_IOBLOBS];
static int next_ioblob = 0;

static void *get_ioblob(const size_t len)
{
    void *ptr = ioblobs[next_ioblob];
    if (len > ioblobs_len[next_ioblob]) {
        //printf("allocating ioblob of %llu bytes...\n", (unsigned long long) len);
        ptr = realloc(ptr, len);
        if (!ptr) {
            fprintf(stderr, APPNAME ": Out of memory!\n");
            quit_altrace_playback();
            _exit(42);
        }
        ioblobs[next_ioblob] = ptr;
        ioblobs_len[next_ioblob] = len;
    }
    next_ioblob++;
    if (next_ioblob >= ((sizeof (ioblobs) / sizeof (ioblobs[0])))) {
        next_ioblob = 0;
    }
    return ptr;
}

typedef struct
{
    ALuint from;
    ALuint to;
} NameMap;


NORETURN static void IO_READ_FAIL(const int eof)
{
    fprintf(stderr, "Failed to read from log: %s\n", eof ? "end of file" : strerror(errno));
    quit_altrace_playback();
    _exit(42);
}

static uint32 readle32(void)
{
    uint32 retval;
    const ssize_t br = read(logfd, &retval, sizeof (retval));
    if (br != ((ssize_t) sizeof (retval))) {
        IO_READ_FAIL(br >= 0);
    }
    return swap32(retval);
}

static uint64 readle64(void)
{
    uint64 retval;
    const ssize_t br = read(logfd, &retval, sizeof (retval));
    if (br != ((ssize_t) sizeof (retval))) {
        IO_READ_FAIL(br >= 0);
    }
    return swap64(retval);
}

static int32 IO_INT32(void)
{
    union { int32 si32; uint32 ui32; } cvt;
    cvt.ui32 = readle32();
    return cvt.si32;
}

static uint32 IO_UINT32(void)
{
    return readle32();
}

static uint64 IO_UINT64(void)
{
    return readle64();
}

static ALCsizei IO_ALCSIZEI(void)
{
    return (ALCsizei) IO_UINT64();
}

static ALsizei IO_ALSIZEI(void)
{
    return (ALsizei) IO_UINT64();
}

static float IO_FLOAT(void)
{
    union { float f; uint32 ui32; } cvt;
    cvt.ui32 = readle32();
    return cvt.f;
}

static double IO_DOUBLE(void)
{
    union { double d; uint64 ui64; } cvt;
    cvt.ui64 = readle64();
    return cvt.d;
}

static uint8 *IO_BLOB(uint64 *_len)
{
    const uint64 len = IO_UINT64();
    const size_t slen = (size_t) len;
    uint8 *ptr;
    ssize_t br;

    if (len == 0xFFFFFFFFFFFFFFFFull) {
        *_len = 0;
        return NULL;
    }

    *_len = len;

    ptr = (uint8 *) get_ioblob(slen + 1);
    br = read(logfd, ptr, slen);
    if (br != ((ssize_t) slen)) {
        IO_READ_FAIL(br >= 0);
    }
    ptr[slen] = '\0';

    return ptr;
}

static const char *IO_STRING(void)
{
    uint64 len;
    return (const char *) IO_BLOB(&len);
}

static EntryEnum IO_ENTRYENUM(void)
{
    return (EntryEnum) IO_UINT32();
}

static void *IO_PTR(void)
{
    return (void *) (size_t) IO_UINT64();
}

static ALCenum IO_ALCENUM(void)
{
    return (ALCenum) IO_UINT32();
}

static ALenum IO_ENUM(void)
{
    return (ALenum) IO_UINT32();
}

static ALCboolean IO_ALCBOOLEAN(void)
{
    return (ALCboolean) IO_UINT32();
}

static ALboolean IO_BOOLEAN(void)
{
    return (ALboolean) IO_UINT32();
}

#define IO_START(e) \
    { \
        if (dump_log) { printf("%s", #e); } { \

#define IO_END() \
        if (dump_log) { fflush(stdout); } } \
    }

static void init_altrace_playback(const char *filename)
{
    int okay = 1;

    fprintf(stderr, APPNAME ": starting up...\n");
    fflush(stderr);

    if (run_log) {
        if (!init_clock()) {
            fflush(stderr);
            _exit(42);
        }

        if (!load_real_openal()) {
            _exit(42);
        }
    }

    if (okay) {
        logfd = open(filename, O_RDONLY);
        if (logfd == -1) {
            fprintf(stderr, APPNAME ": Failed to open OpenAL log file '%s': %s\n", filename, strerror(errno));
            okay = 0;
        } else {
            fprintf(stderr, "\n\n\n" APPNAME ": Playback OpenAL session from log file '%s'\n\n\n", filename);
        }
    }

    fflush(stderr);

    if (okay) {
        if (IO_UINT32() != ALTRACE_LOG_FILE_MAGIC) {
            fprintf(stderr, APPNAME ": File '%s' does not appear to be an OpenAL log file.\n", filename);
            okay = 0;
        } else if (IO_UINT32() != ALTRACE_LOG_FILE_FORMAT) {
            fprintf(stderr, APPNAME ": File '%s' is an unsupported log file format version.\n", filename);
            okay = 0;
        }
    }

    if (!okay) {
        quit_altrace_playback();
        _exit(42);
    }
}

static void quit_altrace_playback(void)
{
    const int io = logfd;
    size_t i;

    logfd = -1;

    fflush(stdout);

    fprintf(stderr, APPNAME ": Shutting down...\n");
    fflush(stderr);

    if ((io != -1) && (close(io) < 0)) {
        fprintf(stderr, APPNAME ": Failed to close OpenAL log file: %s\n", strerror(errno));
    }

    close_real_openal();

    for (i = 0; i < (sizeof (ioblobs) / sizeof (ioblobs[0])); i++) {
        free(ioblobs[i]);
        ioblobs[i] = NULL;
    }
    next_ioblob = 0;

    fflush(stderr);
}

static const char *alcboolString(const ALCboolean x)
{
    char *str;
    switch (x) {
        case ALC_TRUE: return "ALC_TRUE";
        case ALC_FALSE: return "ALC_FALSE";
        default: break;
    }

    str = (char *) get_ioblob(32);
    snprintf(str, 32, "0x%X", (uint) x);
    return str;
}

static const char *alboolString(const ALCboolean x)
{
    char *str;
    switch (x) {
        case AL_TRUE: return "AL_TRUE";
        case AL_FALSE: return "AL_FALSE";
        default: break;
    }

    str = (char *) get_ioblob(32);
    snprintf(str, 32, "0x%X", (uint) x);
    return str;
}

static const char *alcenumString(const ALCenum x)
{
    char *str;
    switch (x) {
        #define ENUM_TEST(e) case e: return #e
        ENUM_TEST(ALC_FREQUENCY);
        ENUM_TEST(ALC_REFRESH);
        ENUM_TEST(ALC_SYNC);
        ENUM_TEST(ALC_MONO_SOURCES);
        ENUM_TEST(ALC_STEREO_SOURCES);
        ENUM_TEST(ALC_NO_ERROR);
        ENUM_TEST(ALC_INVALID_DEVICE);
        ENUM_TEST(ALC_INVALID_CONTEXT);
        ENUM_TEST(ALC_INVALID_ENUM);
        ENUM_TEST(ALC_INVALID_VALUE);
        ENUM_TEST(ALC_OUT_OF_MEMORY);
        ENUM_TEST(ALC_MAJOR_VERSION);
        ENUM_TEST(ALC_MINOR_VERSION);
        ENUM_TEST(ALC_ATTRIBUTES_SIZE);
        ENUM_TEST(ALC_ALL_ATTRIBUTES);
        ENUM_TEST(ALC_DEFAULT_DEVICE_SPECIFIER);
        ENUM_TEST(ALC_DEVICE_SPECIFIER);
        ENUM_TEST(ALC_EXTENSIONS);
        ENUM_TEST(ALC_CAPTURE_DEVICE_SPECIFIER);
        ENUM_TEST(ALC_CAPTURE_DEFAULT_DEVICE_SPECIFIER);
        ENUM_TEST(ALC_CAPTURE_SAMPLES);
        ENUM_TEST(ALC_DEFAULT_ALL_DEVICES_SPECIFIER);
        ENUM_TEST(ALC_ALL_DEVICES_SPECIFIER);
        ENUM_TEST(ALC_CONNECTED);
        #undef ENUM_TEST
        default: break;
    }

    str = (char *) get_ioblob(32);
    snprintf(str, 32, "0x%X", (uint) x);
    return str;
}

static const char *alenumString(const ALCenum x)
{
    char *str;
    switch (x) {
        #define ENUM_TEST(e) case e: return #e
        ENUM_TEST(AL_NONE);
        ENUM_TEST(AL_SOURCE_RELATIVE);
        ENUM_TEST(AL_CONE_INNER_ANGLE);
        ENUM_TEST(AL_CONE_OUTER_ANGLE);
        ENUM_TEST(AL_PITCH);
        ENUM_TEST(AL_POSITION);
        ENUM_TEST(AL_DIRECTION);
        ENUM_TEST(AL_VELOCITY);
        ENUM_TEST(AL_LOOPING);
        ENUM_TEST(AL_BUFFER);
        ENUM_TEST(AL_GAIN);
        ENUM_TEST(AL_MIN_GAIN);
        ENUM_TEST(AL_MAX_GAIN);
        ENUM_TEST(AL_ORIENTATION);
        ENUM_TEST(AL_SOURCE_STATE);
        ENUM_TEST(AL_INITIAL);
        ENUM_TEST(AL_PLAYING);
        ENUM_TEST(AL_PAUSED);
        ENUM_TEST(AL_STOPPED);
        ENUM_TEST(AL_BUFFERS_QUEUED);
        ENUM_TEST(AL_BUFFERS_PROCESSED);
        ENUM_TEST(AL_REFERENCE_DISTANCE);
        ENUM_TEST(AL_ROLLOFF_FACTOR);
        ENUM_TEST(AL_CONE_OUTER_GAIN);
        ENUM_TEST(AL_MAX_DISTANCE);
        ENUM_TEST(AL_SEC_OFFSET);
        ENUM_TEST(AL_SAMPLE_OFFSET);
        ENUM_TEST(AL_BYTE_OFFSET);
        ENUM_TEST(AL_SOURCE_TYPE);
        ENUM_TEST(AL_STATIC);
        ENUM_TEST(AL_STREAMING);
        ENUM_TEST(AL_UNDETERMINED);
        ENUM_TEST(AL_FORMAT_MONO8);
        ENUM_TEST(AL_FORMAT_MONO16);
        ENUM_TEST(AL_FORMAT_STEREO8);
        ENUM_TEST(AL_FORMAT_STEREO16);
        ENUM_TEST(AL_FREQUENCY);
        ENUM_TEST(AL_BITS);
        ENUM_TEST(AL_CHANNELS);
        ENUM_TEST(AL_SIZE);
        ENUM_TEST(AL_UNUSED);
        ENUM_TEST(AL_PENDING);
        ENUM_TEST(AL_PROCESSED);
        ENUM_TEST(AL_INVALID_NAME);
        ENUM_TEST(AL_INVALID_ENUM);
        ENUM_TEST(AL_INVALID_VALUE);
        ENUM_TEST(AL_INVALID_OPERATION);
        ENUM_TEST(AL_OUT_OF_MEMORY);
        ENUM_TEST(AL_VENDOR);
        ENUM_TEST(AL_VERSION);
        ENUM_TEST(AL_RENDERER);
        ENUM_TEST(AL_EXTENSIONS);
        ENUM_TEST(AL_DOPPLER_FACTOR);
        ENUM_TEST(AL_DOPPLER_VELOCITY);
        ENUM_TEST(AL_SPEED_OF_SOUND);
        ENUM_TEST(AL_DISTANCE_MODEL);
        ENUM_TEST(AL_INVERSE_DISTANCE);
        ENUM_TEST(AL_INVERSE_DISTANCE_CLAMPED);
        ENUM_TEST(AL_LINEAR_DISTANCE);
        ENUM_TEST(AL_LINEAR_DISTANCE_CLAMPED);
        ENUM_TEST(AL_EXPONENT_DISTANCE);
        ENUM_TEST(AL_EXPONENT_DISTANCE_CLAMPED);
        ENUM_TEST(AL_FORMAT_MONO_FLOAT32);
        ENUM_TEST(AL_FORMAT_STEREO_FLOAT32);
        #undef ENUM_TEST
        default: break;
    }

    str = (char *) get_ioblob(32);
    snprintf(str, 32, "0x%X", (uint) x);
    return str;
}

static const char *litString(const char *str)
{
    if (str) {
        const size_t slen = (strlen(str) + 1) * 2;
        char *retval = (char *) get_ioblob(slen);
        char *ptr = retval;
        *(ptr++) = '"';
        while (1) {
            const char ch = *(str++);
            if (ch == '\0') {
                *(ptr++) = '"';
                *(ptr++) = '\0';
                break;
            } else if (ch == '"') {
                *(ptr++) = '\\';
                *(ptr++) = '"';
            }
        }
        return retval;
    }
    return "NULL";
}

static void dump_alcGetCurrentContext(void)
{
    IO_START(alcGetCurrentContext);
    ALCcontext *retval = (ALCcontext *) IO_PTR();
    if (dump_log) { printf("() => %p\n", retval); }
    if (run_log) { REAL_alcGetCurrentContext(); }
    IO_END();
}

static void dump_alcGetContextsDevice(void)
{
    IO_START(alcGetContextsDevice);
    ALCcontext *context = (ALCcontext *) IO_PTR();
    ALCdevice *retval = (ALCdevice *) IO_PTR();
    if (dump_log) { printf("(%p) => %p\n", context, retval); }
    //retval = REAL_alcGetContextsDevice(context);
    IO_END();
}

static void dump_alcIsExtensionPresent(void)
{
    IO_START(alcIsExtensionPresent);
    ALCdevice *device = (ALCdevice *) IO_PTR();
    const ALCchar *extname = (const ALCchar *) IO_STRING();
    const ALCboolean retval = IO_ALCBOOLEAN();
    if (dump_log) { printf("(%p, %s) => %s\n", device, litString(extname), alcboolString(retval)); }
    //retval = REAL_alcIsExtensionPresent(device, extname);
    IO_END();
}

static void dump_alcGetProcAddress(void)
{
    IO_START(alcGetProcAddress);
    ALCdevice *device = (ALCdevice *) IO_PTR();
    const ALCchar *funcname = (const ALCchar *) IO_STRING();
    void *retval = IO_PTR();
    if (dump_log) { printf("(%p, %s) => %p\n", device, litString(funcname), retval); }
    IO_END();

}

static void dump_alcGetEnumValue(void)
{
    IO_START(alcGetEnumValue);
    ALCdevice *device = (ALCdevice *) IO_PTR();
    const ALCchar *enumname = (const ALCchar *) IO_STRING();
    const ALCenum retval = IO_ALCENUM();
    if (dump_log) { printf("(%p, %s) => %s\n", device, litString(enumname), alcenumString(retval)); }
    //retval = REAL_alcGetEnumValue(device, enumname);
    IO_END();
}

static void dump_alcGetString(void)
{
    IO_START(alcGetEnumValue);
    ALCdevice *device = (ALCdevice *) IO_PTR();
    const ALCenum param = IO_ALCENUM();
    const ALCchar *retval = (const ALCchar *) IO_STRING();
    if (dump_log) { printf("(%p, %s) => %s\n", device, alcenumString(param), litString(retval)); }
    //retval = REAL_alcGetString(device, param);
    IO_END();
}

static void dump_alcCaptureOpenDevice(void)
{
    IO_START(alcCaptureOpenDevice);
    const ALCchar *devicename = (const ALCchar *) IO_STRING();
    const ALCuint frequency = IO_UINT32();
    const ALCenum format = IO_ALCENUM();
    const ALCsizei buffersize = IO_ALSIZEI();
    ALCdevice *retval = (ALCdevice *) IO_PTR();
    if (dump_log) { printf("(%s, %u, %s, %u) => %p\n", litString(devicename), (uint) frequency, alcenumString(format), (uint) buffersize, retval); }
    //retval = REAL_alcCaptureOpenDevice(devicename, frequency, format, buffersize);
    IO_END();
}

static void dump_alcCaptureCloseDevice(void)
{
    IO_START(alcCaptureCloseDevice);
    ALCdevice *device = (ALCdevice *) IO_PTR();
    const ALCboolean retval = IO_ALCBOOLEAN();
    if (dump_log) { printf("(%p) => %s\n", device, alcboolString(retval)); }
    //retval = REAL_alcCaptureCloseDevice(device);
    IO_END();
}

static void dump_alcOpenDevice(void)
{
    IO_START(alcOpenDevice);
    const ALCchar *devicename = IO_STRING();
    ALCdevice *retval = (ALCdevice *) IO_PTR();
    if (dump_log) { printf("(%s) => %p\n", litString(devicename), retval); }
    //retval = REAL_alcOpenDevice(devicename);
    IO_END();
}

static void dump_alcCloseDevice(void)
{
    IO_START(alcCloseDevice);
    ALCdevice *device = (ALCdevice *) IO_PTR();
    const ALCboolean retval = IO_ALCBOOLEAN();
    if (dump_log) { printf("(%p) => %s\n", device, alcboolString(retval)); }
    //retval = REAL_alcCloseDevice(device);
    IO_END();
}

static void dump_alcCreateContext(void)
{
    IO_START(alcCreateContext);
    ALCcontext *retval;
    ALCint *attrlist = NULL;
    ALCdevice *device = (ALCdevice *) IO_PTR();
    const uint32 attrcount = IO_UINT32();
    if (attrcount) {
        ALCint i;
        attrlist = get_ioblob(sizeof (ALCint) * attrcount);
        for (i = 0; i < attrcount; i++) {
            attrlist[i] = (ALCint) IO_INT32();
        }
    }
    retval = (ALCcontext *) IO_PTR();
    if (dump_log) {
        printf("(%p, ", device);
        if (!attrlist) {
            printf("NULL");
        } else {
            ALCint i;
            printf("{");
            for (i = 0; i < attrcount; i += 2) {
                printf(" %s, %u,", alcenumString(attrlist[i]), (uint) attrlist[i+1]);
            }
            printf(" 0 }");
        }
        printf(") => %p\n", retval);
    }

    //retval = REAL_alcCreateContext(device, attrlist);
    IO_END();

}

static void dump_alcMakeContextCurrent(void)
{
    IO_START(alcMakeContextCurrent);
    ALCcontext *ctx = (ALCcontext *) IO_PTR();
    const ALCboolean retval = IO_ALCBOOLEAN();
    if (dump_log) { printf("(%p) => %s\n", ctx, alcboolString(retval)); }
    //retval = REAL_alcMakeContextCurrent(ctx);
    IO_END();
}

static void dump_alcProcessContext(void)
{
    IO_START(alcProcessContext);
    ALCcontext *ctx = (ALCcontext *) IO_PTR();
    if (dump_log) { printf("(%p)\n", ctx); }
    //REAL_alcProcessContext(ctx);
    IO_END();
}

static void dump_alcSuspendContext(void)
{
    IO_START(alcSuspendContext);
    ALCcontext *ctx = (ALCcontext *) IO_PTR();
    if (dump_log) { printf("(%p)\n", ctx); }
    //REAL_alcSuspendContext(ctx);
    IO_END();
}

static void dump_alcDestroyContext(void)
{
    IO_START(alcDestroyContext);
    ALCcontext *ctx = (ALCcontext *) IO_PTR();
    if (dump_log) { printf("(%p)\n", ctx); }
    //REAL_alcDestroyContext(ctx);
    IO_END();
}

static void dump_alcGetError(void)
{
    IO_START(alcGetError);
    ALCdevice *device = (ALCdevice *) IO_PTR();
    const ALCenum retval = IO_ALCENUM();
    if (dump_log) { printf("(%p) => %s\n", device, alcboolString(retval)); }
    //retval = REAL_alcGetError(device);
    IO_END();
}

static void dump_alcGetIntegerv(void)
{
    IO_START(alcGetIntegerv);
    ALCdevice *device = (ALCdevice *) IO_PTR();
    const ALCenum param = IO_ALCENUM();
    const ALCsizei size = IO_ALCSIZEI();
    if (dump_log) { printf("(%p, %s, %u, &values)\n", device, alcenumString(param), (uint) size); }
    //REAL_alcGetIntegerv(device, param, size, values);
    IO_END();
}

static void dump_alcCaptureStart(void)
{
    IO_START(alcCaptureStart);
    ALCdevice *device = (ALCdevice *) IO_PTR();
    if (dump_log) { printf("(%p)\n", device); }
    //REAL_alcCaptureStart(device);
    IO_END();
}

static void dump_alcCaptureStop(void)
{
    IO_START(alcCaptureStop);
    ALCdevice *device = (ALCdevice *) IO_PTR();
    if (dump_log) { printf("(%p)\n", device); }
    //REAL_alcCaptureStop(device);
    IO_END();
}

static void dump_alcCaptureSamples(void)
{
    IO_START(alcCaptureSamples);
    ALCdevice *device = (ALCdevice *) IO_PTR();
    const ALCsizei samples = IO_ALCSIZEI();
    if (dump_log) { printf("(%p, &buffer, %u)\n", device, (uint) samples); }
    //REAL_alcCaptureSamples(device, buffer, samples);
    IO_END();
}

static void dump_alDopplerFactor(void)
{
    IO_START(alDopplerFactor);
    const ALfloat value = IO_FLOAT();
    if (dump_log) { printf("(%f)\n", value); }
    //REAL_alDopplerFactor(value);
    IO_END();
}

static void dump_alDopplerVelocity(void)
{
    IO_START(alDopplerVelocity);
    const ALfloat value = IO_FLOAT();
    if (dump_log) { printf("(%f)\n", value); }
    //REAL_alDopplerVelocity(value);
    IO_END();
}

static void dump_alSpeedOfSound(void)
{
    IO_START(alSpeedOfSound);
    const ALfloat value = IO_FLOAT();
    if (dump_log) { printf("(%f)\n", value); }
    //REAL_alSpeedOfSound(value);
    IO_END();
}

static void dump_alDistanceModel(void)
{
    IO_START(alDistanceModel);
    const ALenum model = IO_ENUM();
    if (dump_log) { printf("(%s)\n", alenumString(model)); }
    //REAL_alDistanceModel(model);
    IO_END();
}

static void dump_alEnable(void)
{
    IO_START(alEnable);
    const ALenum capability = IO_ENUM();
    if (dump_log) { printf("(%s)\n", alenumString(capability)); }
    //REAL_alEnable(capability);
    IO_END();
}

static void dump_alDisable(void)
{
    IO_START(alDisable);
    const ALenum capability = IO_ENUM();
    if (dump_log) { printf("(%s)\n", alenumString(capability)); }
    //REAL_alDisable(capability);
    IO_END();
}

static void dump_alIsEnabled(void)
{
    IO_START(alIsEnabled);
    const ALenum capability = IO_ENUM();
    const ALboolean retval = IO_BOOLEAN();
    if (dump_log) { printf("(%s) => %s\n", alenumString(capability), alboolString(retval)); }
    //retval = REAL_alIsEnabled(capability);
    IO_END();
}

static void dump_alGetString(void)
{
    IO_START(alGetString);
    const ALenum param = IO_ENUM();
    const ALchar *retval = (const ALchar *) IO_STRING();
    if (dump_log) { printf("(%s) => %s\n", alenumString(param), litString(retval)); }
    //retval = REAL_alGetString(param);
    IO_END();
}

static void dump_alGetBooleanv(void)
{
    IO_START(alGetBooleanv);
    const ALenum param = IO_ENUM();
    if (dump_log) { printf("(%s, &values)\n", alenumString(param)); }
    //REAL_alGetBooleanv(param, values);
    IO_END();
}

static void dump_alGetIntegerv(void)
{
    IO_START(alGetIntegerv);
    const ALenum param = IO_ENUM();
    if (dump_log) { printf("(%s, &values)\n", alenumString(param)); }
    //REAL_alGetIntegerv(param, values);
    IO_END();
}

static void dump_alGetFloatv(void)
{
    IO_START(alGetFloatv);
    const ALenum param = IO_ENUM();
    if (dump_log) { printf("(%s, &values)\n", alenumString(param)); }
    //REAL_alGetFloatv(param, values);
    IO_END();
}

static void dump_alGetDoublev(void)
{
    IO_START(alGetDoublev);
    const ALenum param = IO_ENUM();
    if (dump_log) { printf("(%s, &values)\n", alenumString(param)); }
    //REAL_alGetDoublev(param, values);
    IO_END();
}

static void dump_alGetBoolean(void)
{
    IO_START(alGetBoolean);
    const ALenum param = IO_ENUM();
    const ALboolean retval = IO_BOOLEAN();
    if (dump_log) { printf("(%s) => %s\n", alenumString(param), alboolString(retval)); }
    //retval = REAL_alGetBoolean(param);
    IO_END();
}

static void dump_alGetInteger(void)
{
    IO_START(alGetInteger);
    const ALenum param = IO_ENUM();
    const ALint retval = IO_INT32();
    if (dump_log) { printf("(%s) => %d\n", alenumString(param), (int) retval); }
    //retval = REAL_alGetInteger(param);
    IO_END();
}

static void dump_alGetFloat(void)
{
    IO_START(alGetFloat);
    const ALenum param = IO_ENUM();
    const ALfloat retval = IO_FLOAT();
    if (dump_log) { printf("(%s) => %f\n", alenumString(param), retval); }
    //retval = REAL_alGetFloat(param);
    IO_END();
}

static void dump_alGetDouble(void)
{
    IO_START(alGetDouble);
    const ALenum param = IO_ENUM();
    const ALdouble retval = IO_DOUBLE();
    if (dump_log) { printf("(%s) => %f\n", alenumString(param), (float) retval); }
    //retval = REAL_alGetDouble(param);
    IO_END();
}

static void dump_alIsExtensionPresent(void)
{
    IO_START(alIsExtensionPresent);
    const ALchar *extname = (const ALchar *) IO_STRING();
    const ALboolean retval = IO_BOOLEAN();
    if (dump_log) { printf("(%s) => %s\n", litString(extname), alboolString(retval)); }
    //retval = REAL_alIsExtensionPresent(extname);
    IO_END();
}

static void dump_alGetError(void)
{
    IO_START(alGetError);
    const ALenum retval = IO_ENUM();
    if (dump_log) { printf("() => %s\n", alenumString(retval)); }
    //retval = REAL_alGetError();
    IO_END();
}

static void dump_alGetProcAddress(void)
{
    IO_START(alGetProcAddress);
    const ALchar *funcname = (const ALchar *) IO_STRING();
    void *retval = IO_PTR();
    if (dump_log) { printf("(%s) => %p\n", litString(funcname), retval); }
    IO_END();
}

static void dump_alGetEnumValue(void)
{
    IO_START(alGetProcAddress);
    const ALchar *enumname = (const ALchar *) IO_STRING();
    const ALenum retval = IO_ENUM();
    if (dump_log) { printf("(%s) => %s\n", litString(enumname), alenumString(retval)); }
    IO_END();
}

static void dump_alListenerfv(void)
{
    IO_START(alListenerfv);
    const ALenum param = IO_ENUM();
    const uint32 numvals = IO_UINT32();
    ALfloat *values = (ALfloat *) get_ioblob(sizeof (ALfloat) * numvals);
    uint32 i;

    for (i = 0; i < numvals; i++) {
        values[i] = IO_FLOAT();
    }

    if (dump_log) {
        printf("(%s, {", alenumString(param));
        for (i = 0; i < numvals; i++) {
            printf("%s %f", i > 0 ? "," : "", values[i]);
        }
        printf("%s})\n", numvals > 0 ? " " : "");
    }
    //REAL_alListenerfv(param, values);
    IO_END();
}

static void dump_alListenerf(void)
{
    IO_START(alListenerf);
    const ALenum param = IO_ENUM();
    const ALfloat value = IO_FLOAT();
    if (dump_log) { printf("(%s, %f)\n", alenumString(param), value); }
    //REAL_alListenerf(param, value);
    IO_END();
}

static void dump_alListener3f(void)
{
    IO_START(alListener3f);
    const ALenum param = IO_ENUM();
    const ALfloat value1 = IO_FLOAT();
    const ALfloat value2 = IO_FLOAT();
    const ALfloat value3 = IO_FLOAT();
    if (dump_log) { printf("(%s, %f, %f, %f)\n", alenumString(param), value1, value2, value3); }
    //REAL_alListener3f(param, value1, value2, value3);
    IO_END();
}

static void dump_alListeneriv(void)
{
    IO_START(alListeneriv);
    const ALenum param = IO_ENUM();
    const uint32 numvals = IO_UINT32();
    ALint *values = (ALint *) get_ioblob(sizeof (ALint) * numvals);
    uint32 i;

    for (i = 0; i < numvals; i++) {
        values[i] = IO_INT32();
    }

    if (dump_log) {
        printf("(%s, {", alenumString(param));
        for (i = 0; i < numvals; i++) {
            printf("%s %d", i > 0 ? "," : "", (int) values[i]);
        }
        printf("%s})\n", numvals > 0 ? " " : "");
    }

    //REAL_alListeneriv(param, values);
    IO_END();
}

static void dump_alListeneri(void)
{
    IO_START(alListeneri);
    const ALenum param = IO_ENUM();
    const ALint value = IO_INT32();
    if (dump_log) { printf("(%s, %d)\n", alenumString(param), value); }
    //REAL_alListeneri(param, value);
    IO_END();
}

static void dump_alListener3i(void)
{
    IO_START(alListener3i);
    const ALenum param = IO_ENUM();
    const ALint value1 = IO_INT32();
    const ALint value2 = IO_INT32();
    const ALint value3 = IO_INT32();
    if (dump_log) { printf("(%s, %d, %d, %d)\n", alenumString(param), (int) value1, (int) value2, (int) value3); }
    //REAL_alListener3i(param, value1, value2, value3);
    IO_END();
}

static void dump_alGetListenerfv(void)
{
    IO_START(alGetListenerfv);
    const ALenum param = IO_ENUM();
    if (dump_log) { printf("(%s, &values)\n", alenumString(param)); }
    //REAL_alGetListenerfv(param, values);
    IO_END();
}

static void dump_alGetListenerf(void)
{
    IO_START(alGetListenerf);
    const ALenum param = IO_ENUM();
    if (dump_log) { printf("(%s, &value)\n", alenumString(param)); }
    //REAL_alGetListenerf(param, value);
    IO_END();
}

static void dump_alGetListener3f(void)
{
    IO_START(alGetListener3f);
    const ALenum param = IO_ENUM();
    if (dump_log) { printf("(%s, &value1, &value2, &value3)\n", alenumString(param)); }
    //REAL_alGetListener3f(param, value1, value2, value3);
    IO_END();
}

static void dump_alGetListeneriv(void)
{
    IO_START(alGetListeneriv);
    const ALenum param = IO_ENUM();
    if (dump_log) { printf("(%s, &values)\n", alenumString(param)); }
    //REAL_alGetListeneriv(param, values);
    IO_END();
}

static void dump_alGetListeneri(void)
{
    IO_START(alGetListeneri);
    const ALenum param = IO_ENUM();
    if (dump_log) { printf("(%s, &value)\n", alenumString(param)); }
    //REAL_alGetListeneri(param, value);
    IO_END();
}

static void dump_alGetListener3i(void)
{
    IO_START(alGetListener3i);
    const ALenum param = IO_ENUM();
    if (dump_log) { printf("(%s, &value1, &value2, &value3)\n", alenumString(param)); }
    //REAL_alGetListener3i(param, value1, value2, value3);
    IO_END();
}

static void dump_alGenSources(void)
{
    IO_START(alGenSources);
    const ALsizei n = IO_ALSIZEI();
    ALuint *names = (ALuint *) get_ioblob(sizeof (ALuint) * n);
    ALsizei i;

    for (i = 0; i < n; i++) {
        names[i] = IO_UINT32();
    }

    if (dump_log) {
        printf("(%u) => {", (uint) n);
        for (i = 0; i < n; i++) {
            printf("%s %u", i > 0 ? "," : "", (uint) names[i]);
        }
        printf("%s}\n", n > 0 ? " " : "");
    }

    //REAL_alGenSources(n, names);
    IO_END();
}

static void dump_alDeleteSources(void)
{
    IO_START(alDeleteSources);
    const ALsizei n = IO_ALSIZEI();
    ALuint *names = (ALuint *) get_ioblob(sizeof (ALuint) * n);
    ALsizei i;

    for (i = 0; i < n; i++) {
        names[i] = IO_UINT32();
    }

    if (dump_log) {
        printf("(%u, {", (uint) n);
        for (i = 0; i < n; i++) {
            printf("%s %u", i > 0 ? "," : "", (uint) names[i]);
        }
        printf("%s})\n", n > 0 ? " " : "");
    }

    //REAL_alDeleteSources(n, names);
    IO_END();
}

static void dump_alIsSource(void)
{
    IO_START(alIsSource);
    const ALuint name = IO_UINT32();
    const ALboolean retval = IO_BOOLEAN();
    if (dump_log) { printf("(%u) => %s\n", (uint) name, alboolString(retval)); }
    //retval = REAL_alIsSource(name);
    IO_END();
}

static void dump_alSourcefv(void)
{
    IO_START(alSourcefv);
    const ALuint name = IO_UINT32();
    const ALenum param = IO_ENUM();
    const uint32 numvals = IO_UINT32();
    ALfloat *values = (ALfloat *) get_ioblob(sizeof (ALfloat) * numvals);
    uint32 i;

    for (i = 0; i < numvals; i++) {
        values[i] = IO_FLOAT();
    }

    if (dump_log) {
        printf("(%u, %s, {", (uint) name, alenumString(param));
        for (i = 0; i < numvals; i++) {
            printf("%s %f", i > 0 ? "," : "", values[i]);
        }
        printf("%s})\n", numvals > 0 ? " " : "");
    }

    //REAL_alSourcefv(name, param, values);
    IO_END();
}

static void dump_alSourcef(void)
{
    IO_START(alSourcef);
    const ALuint name = IO_UINT32();
    const ALenum param = IO_ENUM();
    const ALfloat value = IO_FLOAT();
    if (dump_log) { printf("(%u, %s, %f)\n", (uint) name, alenumString(param), value); }
    //REAL_alSourcef(name, param, value);
    IO_END();
}

static void dump_alSource3f(void)
{
    IO_START(alSource3f);
    const ALuint name = IO_UINT32();
    const ALenum param = IO_ENUM();
    const ALfloat value1 = IO_FLOAT();
    const ALfloat value2 = IO_FLOAT();
    const ALfloat value3 = IO_FLOAT();
    if (dump_log) { printf("(%u, %s, %f, %f, %f)\n", (uint) name, alenumString(param), value1, value2, value3); }
    //REAL_alSource3f(name, param, value1, value2, value3);
    IO_END();
}

static void dump_alSourceiv(void)
{
    IO_START(alSourceiv);
    const ALuint name = IO_UINT32();
    const ALenum param = IO_ENUM();
    const uint32 numvals = IO_UINT32();
    ALint *values = (ALint *) get_ioblob(sizeof (ALint) * numvals);
    uint32 i;

    for (i = 0; i < numvals; i++) {
        values[i] = IO_INT32();
    }

    if (dump_log) {
        printf("(%u, %s, {", (uint) name, alenumString(param));
        for (i = 0; i < numvals; i++) {
            printf("%s %d", i > 0 ? "," : "", (int) values[i]);
        }
        printf("%s})\n", numvals > 0 ? " " : "");
    }

    //REAL_alSourceiv(name, param, values);
    IO_END();
}

static void dump_alSourcei(void)
{
    IO_START(alSourcei);
    const ALuint name = IO_UINT32();
    const ALenum param = IO_ENUM();
    const ALint value = IO_INT32();
    if (dump_log) { printf("(%u, %s, %d)\n", (uint) name, alenumString(param), (int) value); }
    //REAL_alSourcei(name, param, value);
    IO_END();
}

static void dump_alSource3i(void)
{
    IO_START(alSource3i);
    const ALuint name = IO_UINT32();
    const ALenum param = IO_ENUM();
    const ALint value1 = IO_INT32();
    const ALint value2 = IO_INT32();
    const ALint value3 = IO_INT32();
    if (dump_log) { printf("(%u, %s, %d, %d, %d)\n", (uint) name, alenumString(param), (int) value1, (int) value2, (int) value3); }
    //REAL_alSource3i(name, param, value1, value2, value3);
    IO_END();
}

static void dump_alGetSourcefv(void)
{
    IO_START(alGetSourcefv);
    const ALuint name = IO_UINT32();
    const ALenum param = IO_ENUM();
    if (dump_log) { printf("(%u, %s, &values)\n", (uint) name, alenumString(param)); }
    //REAL_alGetSourcefv(param, values);
    IO_END();
}

static void dump_alGetSourcef(void)
{
    IO_START(alGetSourcef);
    const ALuint name = IO_UINT32();
    const ALenum param = IO_ENUM();
    if (dump_log) { printf("(%u, %s, &value)\n", (uint) name, alenumString(param)); }
    //REAL_alGetSourcef(name, param, value);
    IO_END();
}

static void dump_alGetSource3f(void)
{
    IO_START(alGetSource3f);
    const ALuint name = IO_UINT32();
    const ALenum param = IO_ENUM();
    if (dump_log) { printf("(%u, %s, &value1, &value2, &value3)\n", (uint) name, alenumString(param)); }
    //REAL_alGetSource3f(name, param, value1, value2, value3);
    IO_END();
}

static void dump_alGetSourceiv(void)
{
    IO_START(alGetSourceiv);
    const ALuint name = IO_UINT32();
    const ALenum param = IO_ENUM();
    if (dump_log) { printf("(%u, %s, &values)\n", (uint) name, alenumString(param)); }
    //REAL_alGetSourceiv(name, param, values);
    IO_END();
}

static void dump_alGetSourcei(void)
{
    IO_START(alGetSourcei);
    const ALuint name = IO_UINT32();
    const ALenum param = IO_ENUM();
    if (dump_log) { printf("(%u, %s, &value)\n", (uint) name, alenumString(param)); }
    //REAL_alGetSourcei(name, param, value);
    IO_END();
}

static void dump_alGetSource3i(void)
{
    IO_START(alGetSource3i);
    const ALuint name = IO_UINT32();
    const ALenum param = IO_ENUM();
    if (dump_log) { printf("(%u, %s, &value1, &value2, &value3)\n", (uint) name, alenumString(param)); }
    //REAL_alGetSource3i(param, value1, value2, value3);
    IO_END();
}

static void dump_alSourcePlay(void)
{
    IO_START(alSourcePlay);
    const ALuint name = IO_UINT32();
    if (dump_log) { printf("(%u)\n", (uint) name); }
    //REAL_alSourcePlay(name);
    IO_END();
}

static void dump_alSourcePlayv(void)
{
    IO_START(alSourcePlayv);
    const ALsizei n = IO_ALSIZEI();
    ALuint *names = (ALuint *) get_ioblob(sizeof (ALuint) * n);
    ALsizei i;

    for (i = 0; i < n; i++) {
        names[i] = IO_UINT32();
    }

    if (dump_log) {
        printf("(%u, {", (uint) n);
        for (i = 0; i < n; i++) {
            printf("%s %u", i > 0 ? "," : "", (uint) names[i]);
        }
        printf("%s})\n", n > 0 ? " " : "");
    }

    //REAL_alSourcePlayv(n, names);
    IO_END();
}

static void dump_alSourcePause(void)
{
    IO_START(alSourcePause);
    const ALuint name = IO_UINT32();
    if (dump_log) { printf("(%u)\n", (uint) name); }
    //REAL_alSourcePause(name);
    IO_END();
}

static void dump_alSourcePausev(void)
{
    IO_START(alSourcePausev);
    const ALsizei n = IO_ALSIZEI();
    ALuint *names = (ALuint *) get_ioblob(sizeof (ALuint) * n);
    ALsizei i;

    for (i = 0; i < n; i++) {
        names[i] = IO_UINT32();
    }

    if (dump_log) {
        printf("(%u, {", (uint) n);
        for (i = 0; i < n; i++) {
            printf("%s %u", i > 0 ? "," : "", (uint) names[i]);
        }
        printf("%s})\n", n > 0 ? " " : "");
    }

    //REAL_alSourcePausev(n, names);
    IO_END();
}

static void dump_alSourceRewind(void)
{
    IO_START(alSourceRewind);
    const ALuint name = IO_UINT32();
    if (dump_log) { printf("(%u)\n", (uint) name); }
    //REAL_alSourceRewind(name);
    IO_END();
}

static void dump_alSourceRewindv(void)
{
    IO_START(alSourceRewindv);
    const ALsizei n = IO_ALSIZEI();
    ALuint *names = (ALuint *) get_ioblob(sizeof (ALuint) * n);
    ALsizei i;

    for (i = 0; i < n; i++) {
        names[i] = IO_UINT32();
    }

    if (dump_log) {
        printf("(%u, {", (uint) n);
        for (i = 0; i < n; i++) {
            printf("%s %u", i > 0 ? "," : "", (uint) names[i]);
        }
        printf("%s})\n", n > 0 ? " " : "");
    }

    //REAL_alSourceRewindv(n, names);
    IO_END();
}

static void dump_alSourceStop(void)
{
    IO_START(alSourceStop);
    const ALuint name = IO_UINT32();
    if (dump_log) { printf("(%u)\n", (uint) name); }
    //REAL_alSourceStop(name);
    IO_END();
}

static void dump_alSourceStopv(void)
{
    IO_START(alSourceStopv);
    const ALsizei n = IO_ALSIZEI();
    ALuint *names = (ALuint *) get_ioblob(sizeof (ALuint) * n);
    ALsizei i;

    for (i = 0; i < n; i++) {
        names[i] = IO_UINT32();
    }

    if (dump_log) {
        printf("(%u, {", (uint) n);
        for (i = 0; i < n; i++) {
            printf("%s %u", i > 0 ? "," : "", (uint) names[i]);
        }
        printf("%s})\n", n > 0 ? " " : "");
    }

    //REAL_alSourceStopv(n, names);
    IO_END();
}

static void dump_alSourceQueueBuffers(void)
{
    IO_START(alSourceQueueBuffers);
    const ALuint name = IO_UINT32();
    const ALsizei nb = IO_ALSIZEI();
    ALuint *names = (ALuint *) get_ioblob(sizeof (ALuint) * nb);
    ALsizei i;

    for (i = 0; i < nb; i++) {
        names[i] = IO_UINT32();
    }

    if (dump_log) {
        printf("(%u, %u, {", (uint) name, (uint) nb);
        for (i = 0; i < nb; i++) {
            printf("%s %u", i > 0 ? "," : "", (uint) names[i]);
        }
        printf("%s})\n", nb > 0 ? " " : "");
    }

    //REAL_alSourceQueueBuffers(name, nb, names);
    IO_END();
}

static void dump_alSourceUnqueueBuffers(void)
{
    IO_START(alSourceUnqueueBuffers);
    const ALuint name = IO_UINT32();
    const ALsizei nb = IO_ALSIZEI();
    ALuint *names = (ALuint *) get_ioblob(sizeof (ALuint) * nb);
    ALsizei i;

    for (i = 0; i < nb; i++) {
        names[i] = IO_UINT32();
    }

    if (dump_log) {
        printf("(%u, %u, &names) => {", (uint) name, (uint) nb);
        for (i = 0; i < nb; i++) {
            printf("%s %u", i > 0 ? "," : "", (uint) names[i]);
        }
        printf("%s})\n", nb > 0 ? " " : "");
    }

    //REAL_alSourceUnqueueBuffers(name, nb, names);
    IO_END();
}

static void dump_alGenBuffers(void)
{
    IO_START(alGenBuffers);
    const ALsizei n = IO_ALSIZEI();
    ALuint *names = (ALuint *) get_ioblob(sizeof (ALuint) * n);
    ALsizei i;

    for (i = 0; i < n; i++) {
        names[i] = IO_UINT32();
    }

    if (dump_log) {
        printf("(%u) => {", (uint) n);
        for (i = 0; i < n; i++) {
            printf("%s %u", i > 0 ? "," : "", (uint) names[i]);
        }
        printf("%s}\n", n > 0 ? " " : "");
    }

    //REAL_alGenBuffers(n, names);
    IO_END();
}

static void dump_alDeleteBuffers(void)
{
    IO_START(alDeleteBuffers);
    const ALsizei n = IO_ALSIZEI();
    ALuint *names = (ALuint *) get_ioblob(sizeof (ALuint) * n);
    ALsizei i;

    for (i = 0; i < n; i++) {
        names[i] = IO_UINT32();
    }

    if (dump_log) {
        printf("(%u, {", (uint) n);
        for (i = 0; i < n; i++) {
            printf("%s %u", i > 0 ? "," : "", (uint) names[i]);
        }
        printf("%s})\n", n > 0 ? " " : "");
    }

    //REAL_alDeleteBuffers(n, names);
    IO_END();
}

static void dump_alIsBuffer(void)
{
    IO_START(alIsBuffer);
    const ALuint name = IO_UINT32();
    const ALboolean retval = IO_BOOLEAN();
    if (dump_log) { printf("(%u) => %s\n", (uint) name, alboolString(retval)); }
    //retval = REAL_alIsBuffer(name);
    IO_END();
}

static void dump_alBufferData(void)
{
    IO_START(alBufferData);
    uint64 size = 0;
    const ALuint name = IO_UINT32();
    const ALenum alfmt = IO_ENUM();
    const ALsizei freq = IO_ALSIZEI();
    const ALvoid *data = (const ALvoid *) IO_BLOB(&size); (void) data;
    if (dump_log) { printf("(%u, %s, &data, %u, %u)\n", (uint) name, alenumString(alfmt), (uint) size, (uint) freq); }
    //REAL_alBufferData(name, alfmt, data, (ALsizei) size, freq);
    IO_END();
}

static void dump_alBufferfv(void)
{
    IO_START(alBufferfv);
    const ALuint name = IO_UINT32();
    const ALenum param = IO_ENUM();
    const uint32 numvals = IO_UINT32();
    ALfloat *values = (ALfloat *) get_ioblob(sizeof (ALfloat) * numvals);
    uint32 i;

    for (i = 0; i < numvals; i++) {
        values[i] = IO_INT32();
    }

    if (dump_log) {
        printf("(%u, %s, {", (uint) name, alenumString(param));
        for (i = 0; i < numvals; i++) {
            printf("%s %d", i > 0 ? "," : "", (int) values[i]);
        }
        printf("%s})\n", numvals > 0 ? " " : "");
    }

    //REAL_alBufferfv(name, param, values);
    IO_END();
}

static void dump_alBufferf(void)
{
    IO_START(alBufferf);
    const ALuint name = IO_UINT32();
    const ALenum param = IO_ENUM();
    const ALfloat value = IO_FLOAT();
    if (dump_log) { printf("(%u, %s, %f)\n", (uint) name, alenumString(param), value); }
    //REAL_alBufferf(name, param, value);
    IO_END();
}

static void dump_alBuffer3f(void)
{
    IO_START(alBuffer3f);
    const ALuint name = IO_UINT32();
    const ALenum param = IO_ENUM();
    const ALfloat value1 = IO_FLOAT();
    const ALfloat value2 = IO_FLOAT();
    const ALfloat value3 = IO_FLOAT();
    if (dump_log) { printf("(%u, %s, %f, %f, %f)\n", (uint) name, alenumString(param), value1, value2, value3); }
    //REAL_alBuffer3f(name, param, value1, value2, value3);
    IO_END();
}

static void dump_alBufferiv(void)
{
    IO_START(alBufferiv);
    const ALuint name = IO_UINT32();
    const ALenum param = IO_ENUM();
    const uint32 numvals = IO_UINT32();
    ALint *values = (ALint *) get_ioblob(sizeof (ALint) * numvals);
    uint32 i;

    for (i = 0; i < numvals; i++) {
        values[i] = IO_INT32();
    }

    if (dump_log) {
        printf("(%u, %s, {", (uint) name, alenumString(param));
        for (i = 0; i < numvals; i++) {
            printf("%s %d", i > 0 ? "," : "", (int) values[i]);
        }
        printf("%s})\n", numvals > 0 ? " " : "");
    }

    //REAL_alBufferiv(name, param, values);
    IO_END();
}

static void dump_alBufferi(void)
{
    IO_START(alBufferi);
    const ALuint name = IO_UINT32();
    const ALenum param = IO_ENUM();
    const ALint value = IO_INT32();
    if (dump_log) { printf("(%u, %s, %d)\n", (uint) name, alenumString(param), (int) value); }
    //REAL_alBufferi(name, param, value);
    IO_END();
}

static void dump_alBuffer3i(void)
{
    IO_START(alBuffer3i);
    const ALuint name = IO_UINT32();
    const ALenum param = IO_ENUM();
    const ALint value1 = IO_INT32();
    const ALint value2 = IO_INT32();
    const ALint value3 = IO_INT32();
    if (dump_log) { printf("(%u, %s, %d, %d, %d)\n", (uint) name, alenumString(param), (int) value1, (int) value2, (int) value3); }
    //REAL_alBuffer3i(name, param, value1, value2, value3);
    IO_END();
}

static void dump_alGetBufferfv(void)
{
    IO_START(alGetBufferfv);
    const ALuint name = IO_UINT32();
    const ALenum param = IO_ENUM();
    if (dump_log) { printf("(%u, %s, &values)\n", (uint) name, alenumString(param)); }
    //REAL_alGetBufferfv(param, values);
    IO_END();
}

static void dump_alGetBufferf(void)
{
    IO_START(alGetBufferf);
    const ALuint name = IO_UINT32();
    const ALenum param = IO_ENUM();
    if (dump_log) { printf("(%u, %s, &value)\n", (uint) name, alenumString(param)); }
    //REAL_alGetBufferf(name, param, value);
    IO_END();
}

static void dump_alGetBuffer3f(void)
{
    IO_START(alGetBuffer3f);
    const ALuint name = IO_UINT32();
    const ALenum param = IO_ENUM();
    if (dump_log) { printf("(%u, %s, &value1, &value2, &value3)\n", (uint) name, alenumString(param)); }
    //REAL_alGetBuffer3f(name, param, value1, value2, value3);
    IO_END();
}

static void dump_alGetBufferi(void)
{
    IO_START(alGetBufferi);
    const ALuint name = IO_UINT32();
    const ALenum param = IO_ENUM();
    if (dump_log) { printf("(%u, %s, &value)\n", (uint) name, alenumString(param)); }
    //REAL_alGetBufferi(name, param, value);
    IO_END();
}

static void dump_alGetBuffer3i(void)
{
    IO_START(alGetBuffer3i);
    const ALuint name = IO_UINT32();
    const ALenum param = IO_ENUM();
    if (dump_log) { printf("(%u, %s, &value1, &value2, &value3)\n", (uint) name, alenumString(param)); }
    //REAL_alGetBuffer3i(param, value1, value2, value3);
    IO_END();
}

static void dump_alGetBufferiv(void)
{
    IO_START(alGetBufferiv);
    const ALuint name = IO_UINT32();
    const ALenum param = IO_ENUM();
    if (dump_log) { printf("(%u, %s, &values)\n", (uint) name, alenumString(param)); }
    //REAL_alGetBufferiv(param, values);
    IO_END();
}



static void dump_al_error_event(void)
{
    const ALenum err = IO_ENUM();
    if (dump_log) {
        printf("<<< AL ERROR SET HERE: %s >>>\n", alenumString(err));
    }
}

static void dump_alc_error_event(void)
{
    const ALenum err = IO_ENUM();
    if (dump_log) {
        printf("<<< ALC ERROR SET HERE: %s >>>\n", alcenumString(err));
    }
}

static void process_log(void)
{
    int eos = 0;
    while (!eos) {
        const uint32 wait_until = IO_UINT32();
        if (run_log) {
            while (NOW() < wait_until) {
                usleep(1000);  /* keep the pace of the original run */
            }
        }

        switch (IO_ENTRYENUM()) {
            #define ENTRYPOINT(ret,name,params,args) case ALEE_##name: dump_##name(); break;
            #include "altrace_entrypoints.h"

            case ALEE_ALERROR_EVENT:
                dump_al_error_event();
                break;

            case ALEE_ALCERROR_EVENT:
                dump_alc_error_event();
                break;

            case ALEE_EOS:
                if (dump_log) { printf("\n<<< END OF LOG FILE >>>\n"); }
                eos = 1;
                break;

            default:
                printf("\n<<< UNEXPECTED LOG ENTRY. BUG? NEW LOG VERSION? CORRUPT FILE? >>>\n");
                eos = 1;
                break;
        }
    }
}

int main(int argc, char **argv)
{
    const char *fname = NULL;
    int usage = 0;
    int i;

    for (i = 1; i < argc; i++) {
        const char *arg = argv[i];
        if (strcmp(arg, "--dump") == 0) {
            dump_log = 1;
        } else if (strcmp(arg, "--no-dump") == 0) {
            dump_log = 0;
        } else if (strcmp(arg, "--run") == 0) {
            run_log = 1;
        } else if (strcmp(arg, "--no-run") == 0) {
            run_log = 0;
        } else if (fname == NULL) {
            fname = arg;
        } else {
            usage = 1;
        }
    }

    if (fname == NULL) {
        usage = 1;
    }

    if (usage) {
        fprintf(stderr, "USAGE: %s [--[no-]dump] [--[no-]run] <altrace.trace>\n", argv[0]);
        return 1;
    }

    init_altrace_playback(fname);
    process_log();
    quit_altrace_playback();
    return 0;
}

// end of altrace_playback.c ...
