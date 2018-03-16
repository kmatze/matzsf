// matzsf.c

// #include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <windows.h>
#include <mmsystem.h>
#include <tcl.h>

#define NS "matzsf"
#define VERSION "1.006"
#define VERSDATUM "15-03-2018"

enum {STOP, PLAY, PAUSE};
enum {UNLOAD, LOAD};
static int stsPlayer;			// STOP, PLAY, PAUSE
static int stsSF2;				// LOAD, UNLOAD
static int stsMID;				// LOAD, UNLOAD


#include "minisdl_audio.h"
#define TSF_IMPLEMENTATION
#include "tsf.h"
#define TML_IMPLEMENTATION
#include "tml.h"

static int    g_MsecLength;                 	// whole playback time
static int    g_Mpresets;                       // number of presets in Soundfont
static double g_Msec;                 			// current playback time
static int    g_Mchn;							//                  channel
static int    g_Mkey;							//                  key
static int    g_Mvel;							//                  velocity
static int    g_Mdur;                           //                  duration
static tml_message* g_MidiMessage;        		// next message to be played
static SDL_AudioSpec OutputAudioSpec;          	// SDL device	
static tml_message* TinyMidiLoader = NULL;     	// Holds the global midi instance pointer
static tsf* g_TinySoundFont = TSF_NULL;   		// Holds the global soundfont instance pointer

// tcl macros
//
#define TCL_MSG(m)      	Tcl_AppendResult(interp, fstring("%s", m), NULL);
#define TCL_ERR(m)      	{ Tcl_AppendResult(interp, fstring("%s", m), NULL); return TCL_ERROR; }
#define CHECK_ARGS(n,m) 	if (argc != (n) + 1) TCL_ERR(fstring("wrong arguments: %s", m));
#define BUFFLEN 255


/****************************************************************************************
 *
 * c procedures
 *
 ****************************************************************************************/

 // generate one string from format with n format strings
const char* fstring( char* fmt, ...)
{
    // eg.: fstring("%s", "zf> error: ");
    static char buf[BUFFLEN];
    va_list vl;
    va_start(vl, fmt);
    vsnprintf( buf, sizeof( buf), fmt, vl);
    va_end( vl);
    return buf;
}

// stop playing routine and reset synthesizer
static void stop_the_playing()
{
	SDL_PauseAudio(1);
	
	// reset the synthesizer 
	for (int i = 0; i < 16; i++) 
	{
		tsf_channel_note_off_all(g_TinySoundFont, i);
		tsf_channel_set_preset(g_TinySoundFont, i, 0);
		tsf_channel_set_pitchwheel(g_TinySoundFont, i, 8192);
		tsf_channel_set_pan(g_TinySoundFont, i, .5f);
		tsf_channel_set_volume(g_TinySoundFont, i, 1.f);
	}

	g_Msec = 0; g_Mchn = 0; g_Mkey = 0; g_Mvel = 0; g_Mdur = 0;
	stsPlayer = STOP;
}

// Callback function called by the audio thread
static void AudioCallback(void* data, Uint8 *stream, int len)
{
	//Number of samples to process
	int SampleBlock, SampleCount = (len / (2 * sizeof(float))); //2 output channels
	for (SampleBlock = TSF_RENDER_EFFECTSAMPLEBLOCK; SampleCount; SampleCount -= SampleBlock, stream += (SampleBlock * (2 * sizeof(float))))
	{
		//We progress the MIDI playback and then process TSF_RENDER_EFFECTSAMPLEBLOCK samples at once
		if (SampleBlock > SampleCount) SampleBlock = SampleCount;

		//Loop through all MIDI messages which need to be played up until the current playback time
		for (g_Msec += SampleBlock * (1000.0 / 44100.0); g_MidiMessage && g_Msec >= g_MidiMessage->time; g_MidiMessage = g_MidiMessage->next)
		{
			int Preset;
			switch (g_MidiMessage->type)
			{
				case TML_PROGRAM_CHANGE: //channel program (preset) change
					if (g_MidiMessage->channel == 9)
					{
						//10th MIDI channel uses percussion sound bank (128)
						Preset = tsf_get_presetindex(g_TinySoundFont, 128, g_MidiMessage->program);
						if (Preset < 0) Preset = tsf_get_presetindex(g_TinySoundFont, 128, 0);
						if (Preset < 0) Preset = tsf_get_presetindex(g_TinySoundFont, 0, g_MidiMessage->program);
					}
					else Preset = tsf_get_presetindex(g_TinySoundFont, 0, g_MidiMessage->program);
					tsf_channel_set_preset(g_TinySoundFont, g_MidiMessage->channel, (Preset < 0 ? 0 : Preset));
					break;
				case TML_NOTE_ON: //play a note
					tsf_channel_note_on(g_TinySoundFont, g_MidiMessage->channel, g_MidiMessage->key, g_MidiMessage->velocity / 127.0f);
					//
					// matzsf
					//
					g_Mchn = g_MidiMessage->channel;   g_Mkey = g_MidiMessage->key; 
					g_Mvel = g_MidiMessage->velocity;  g_Mdur = g_MidiMessage->time;
					//
					break;
				case TML_NOTE_OFF: //stop a note
					tsf_channel_note_off(g_TinySoundFont, g_MidiMessage->channel, g_MidiMessage->key);
					//					//
					// matzsf
					//
					g_Mchn = g_MidiMessage->channel;   g_Mkey = g_MidiMessage->key; 
					g_Mvel = g_MidiMessage->velocity;  g_Mdur = g_MidiMessage->time;
					//
					break;
				case TML_PITCH_BEND: //pitch wheel modification
					tsf_channel_set_pitchwheel(g_TinySoundFont, g_MidiMessage->channel, g_MidiMessage->pitch_bend);
					break;
				case TML_CONTROL_CHANGE: //MIDI controller messages
					switch (g_MidiMessage->control)
					{
						#define FLOAT_APPLY_MSB(val, msb) (((((int)(val*16383.5f)) &  0x7f) | (msb << 7)) / 16383.0f)
						#define FLOAT_APPLY_LSB(val, lsb) (((((int)(val*16383.5f)) & ~0x7f) |  lsb      ) / 16383.0f)
						case TML_VOLUME_MSB: case TML_EXPRESSION_MSB:
							tsf_channel_set_volume(g_TinySoundFont, g_MidiMessage->channel, FLOAT_APPLY_MSB(tsf_channel_get_volume(g_TinySoundFont, g_MidiMessage->channel), g_MidiMessage->control_value));
							break;
						case TML_VOLUME_LSB: case TML_EXPRESSION_LSB:
							tsf_channel_set_volume(g_TinySoundFont, g_MidiMessage->channel, FLOAT_APPLY_LSB(tsf_channel_get_volume(g_TinySoundFont, g_MidiMessage->channel), g_MidiMessage->control_value));
							break;
						case TML_BALANCE_MSB: case TML_PAN_MSB: 
							tsf_channel_set_pan(g_TinySoundFont, g_MidiMessage->channel, FLOAT_APPLY_MSB(tsf_channel_get_pan(g_TinySoundFont, g_MidiMessage->channel), g_MidiMessage->control_value));
							break;
						case TML_BALANCE_LSB: case TML_PAN_LSB:
							tsf_channel_set_pan(g_TinySoundFont, g_MidiMessage->channel, FLOAT_APPLY_LSB(tsf_channel_get_pan(g_TinySoundFont, g_MidiMessage->channel), g_MidiMessage->control_value));
							break;
						case TML_ALL_SOUND_OFF:
							tsf_channel_sounds_off_all(g_TinySoundFont, g_MidiMessage->channel);
							break;
						case TML_ALL_CTRL_OFF:
							tsf_channel_set_volume(g_TinySoundFont, g_MidiMessage->channel, 1.0f);
							tsf_channel_set_pan(g_TinySoundFont, g_MidiMessage->channel, 0.5f);
							break;
						case TML_ALL_NOTES_OFF:
							tsf_channel_note_off_all(g_TinySoundFont, g_MidiMessage->channel);
							break;
					}
					break;
			}
		}

		// Render the block of audio samples in float format
		tsf_render_float(g_TinySoundFont, (float*)stream, SampleBlock, 0);
	}
	//
	// matzsf
	//
	// test if ending the playing time
	if (g_Msec > g_MsecLength) stop_the_playing();
}

/****************************************************************************************
 *
 * tcl procedures
 *
 ****************************************************************************************/

int Help_cmd(ClientData cdata, Tcl_Interp *interp, int argc, char *argv[])
{
	TCL_MSG("-----------------------------------------------------------------------------\n");
	TCL_MSG("tcl package to to use soundfont " VERSION ", " VERSDATUM " (c)2018 ma.ke.\n");
	TCL_MSG("based on TinySoundFont v0.8 synthesizer by Bernhard Schelling\n");
	TCL_MSG("         https://github.com/schellingb/TinySoundFont\n");
	TCL_MSG("-----------------------------------------------------------------------------\n");	
	TCL_MSG(NS "::help\n");
	TCL_MSG(NS "::status         -> get list (player, soundfont, #presets, midifile)  \n");
	TCL_MSG(NS "::loadSF2 file   -> get list of presets\n");
	TCL_MSG(NS "::loadMID file   -> get list (#chn, #prgs, #nts, first_msec, length_msec)\n");
	TCL_MSG(NS "::play           -> play loaded midifile\n");
	TCL_MSG(NS "::playinfo       -> get list (playtime_msec, chn, key, vel, dur)\n");
	TCL_MSG(NS "::pause          -> toogle command to pause and continue\n");
	TCL_MSG(NS "::stop\n");
	TCL_MSG("-----------------------------------------------------------------------------\n");
	TCL_MSG(NS "::mci \"mciSendString\"\n");
	
	return TCL_OK;
}

int Status_cmd(ClientData clientData, Tcl_Interp *interp, int argc, char *argv[])
{	
	TCL_MSG(fstring("%i %i %i %i" ,stsPlayer, stsSF2, g_Mpresets, stsMID));
	return TCL_OK;	
}

int LoadSF2_cmd(ClientData clientData, Tcl_Interp *interp, int argc, char *argv[])
{
	CHECK_ARGS(1, "sf2 | mem");
	if (stsPlayer != STOP)           	TCL_ERR("tcl error: only in STOP modus");
	
	tsf_close(g_TinySoundFont);
	g_TinySoundFont = TSF_NULL;
	stsSF2 = UNLOAD;
	int i;
	
	// open and load soundfont file 
	g_TinySoundFont = tsf_load_filename(argv[1]);
	if (!g_TinySoundFont)  				TCL_ERR("tcl error: Could not load soundfont file");
	
	stsSF2 = LOAD;	
	//Initialize preset on special 10th MIDI channel to use percussion sound bank (128) if available
	tsf_channel_set_bank_preset(g_TinySoundFont, 9, 128, 0);
	
	// Set the SoundFont rendering output mode with -18 db volume gain
	tsf_set_output(g_TinySoundFont, TSF_STEREO_INTERLEAVED, OutputAudioSpec.freq, -18.0f);
	
	// returns list of presets
	for (i = 0, g_Mpresets = tsf_get_presetcount(g_TinySoundFont); i < g_Mpresets; i++)
		TCL_MSG(fstring("{%s} ", tsf_get_presetname(g_TinySoundFont, i)));
	
	return TCL_OK;	
}

int LoadMID_cmd(ClientData clientData, Tcl_Interp *interp, int argc, char *argv[])
{
	CHECK_ARGS(1, "mid | mem");
	if (stsPlayer != STOP)           TCL_ERR("tcl error: only in STOP modus");

	int Channels, Programs, Notes, FirstNote, Length;
	tml_free(TinyMidiLoader);
	TinyMidiLoader = NULL;
	g_MidiMessage = NULL;
	g_MsecLength = 0;
	g_Msec = 0; g_Mchn = 0; g_Mkey = 0; g_Mvel = 0; g_Mdur = 0;
	stsMID = UNLOAD;
	
	// open and load midi file
	TinyMidiLoader = tml_load_filename(argv[1]);	
	if (!TinyMidiLoader)  TCL_ERR("tcl error: Could not load Midi file");	
	stsMID = LOAD;
	
	// Get infos about this loaded MIDI file, returns the note count
	// NULL can be passed for any output value pointer if not needed.
	//   used_channels:   Will be set to how many channels play notes
	//                    (i.e. 1 if channel 15 is used but no other)
	//   used_programs:   Will be set to how many different programs are used
	//   total_notes:     Will be set to the total number of note on messages
	//   time_first_note: Will be set to the time of the first note on message
	//   time_length:     Will be set to the total time in milliseconds	
	tml_get_info(TinyMidiLoader, &Channels, &Programs, &Notes, &FirstNote, &Length);
	g_MsecLength = Length;
	
	TCL_MSG(fstring("%i %i %i %i %i" ,Channels, Programs, Notes, FirstNote, g_MsecLength));
	return TCL_OK;	
}

int Play_cmd(ClientData clientData, Tcl_Interp *interp, int argc, char *argv[])
{
	if (stsPlayer != STOP)           TCL_ERR("tcl error: only in STOP modus");
	if (stsSF2    == UNLOAD)         TCL_ERR("tcl error: no soundfont loaded");
	if (stsMID    == UNLOAD)         TCL_ERR("tcl error: no midifile loaded");
	
	//Set up the global MidiMessage pointer to the first MIDI message
	g_MidiMessage = TinyMidiLoader;
	
	SDL_PauseAudio(0);
	stsPlayer = PLAY;
	
	TCL_MSG(fstring("%i" ,stsPlayer));
	return TCL_OK;
}

int PlayInfo_cmd(ClientData clientData, Tcl_Interp *interp, int argc, char *argv[])
{	
	if (stsSF2    == UNLOAD)         TCL_ERR("tcl error: no soundfont loaded");
	
	TCL_MSG(fstring("%f %i %i %i %i" ,g_Msec, g_Mchn, g_Mkey, g_Mvel, g_Mdur));

	return TCL_OK;
}

int Pause_cmd(ClientData clientData, Tcl_Interp *interp, int argc, char *argv[])
{
	if (stsPlayer == STOP) TCL_ERR("tcl error: only in PLAY or PAUSE modus");
	
	if (stsPlayer == PLAY) stsPlayer = PAUSE; else stsPlayer = PLAY;
	if (stsPlayer == PLAY) SDL_PauseAudio(0); else SDL_PauseAudio(1);
	
	TCL_MSG(fstring("%i" ,stsPlayer));
	return TCL_OK;
}

int Stop_cmd(ClientData clientData, Tcl_Interp *interp, int argc, char *argv[])
{	
	if (stsPlayer == STOP)           return TCL_OK;	
	stop_the_playing();	
	TCL_MSG(fstring("%i" ,stsPlayer));
	return TCL_OK;	
}

int Mci_cmd(ClientData clientData, Tcl_Interp *interp, int argc, char *argv[])
{
	CHECK_ARGS(1, "[\"mciSendString\"]")

	char *mci = argv[1];
	char ret[BUFFLEN];
	char buffer[BUFFLEN];
	DWORD err;

	//MCI_MEMFILE_START;
		
	if((err = mciSendString(mci, ret, sizeof(ret), NULL))) 
	{ 
		TCL_MSG(fstring("tcl error: 0x%08X from mci", err));
		if ((mciGetErrorString(err, &buffer[0], sizeof(buffer))))  TCL_MSG (fstring("\nmci error: %s -> %s", mci, buffer));
		return TCL_ERROR;
	}

	TCL_MSG(fstring("%s", ret));
	return TCL_OK;
}

//***********************************************************************************************************************************************

int DLLEXPORT Matzsf_Init(Tcl_Interp *interp)
{
	if (Tcl_InitStubs (interp, TCL_VERSION, 0) == NULL)   { return TCL_ERROR; }
	if (Tcl_PkgProvide(interp, NS, VERSION) == TCL_ERROR) { return TCL_ERROR; }

	// Initialize the audio system
	OutputAudioSpec.freq = 44100;
	OutputAudioSpec.format = AUDIO_F32;
	OutputAudioSpec.channels = 2;
	OutputAudioSpec.samples = 4096;
	OutputAudioSpec.callback = AudioCallback;
	
	// Request the desired audio output format	
	if (SDL_AudioInit(NULL) < 0)  	TCL_ERR("tcl error: Could not initialize audio hardware or driver");
	if (SDL_OpenAudio(&OutputAudioSpec, TSF_NULL) < 0)  TCL_ERR("tcl error: Could not open the audio hardware or the desired audio output format");

	// initialize global parameter for playing
	stsPlayer = STOP;
	stsSF2    = UNLOAD;
	stsMID    = UNLOAD;
	
	// Tcl_LinkVar(interp, "__PLAYER__", (char *) &stsPlayer, TCL_LINK_INT);
	
	Tcl_CreateCommand(interp, NS "::help",        (Tcl_CmdProc*)Help_cmd, NULL, NULL);
	Tcl_CreateCommand(interp, NS "::status",      (Tcl_CmdProc*)Status_cmd, NULL, NULL);
	Tcl_CreateCommand(interp, NS "::loadSF2",     (Tcl_CmdProc*)LoadSF2_cmd, NULL, NULL);
	Tcl_CreateCommand(interp, NS "::loadMID",     (Tcl_CmdProc*)LoadMID_cmd, NULL, NULL);
	Tcl_CreateCommand(interp, NS "::play",        (Tcl_CmdProc*)Play_cmd, NULL, NULL);
	Tcl_CreateCommand(interp, NS "::playinfo",    (Tcl_CmdProc*)PlayInfo_cmd, NULL, NULL);
	Tcl_CreateCommand(interp, NS "::pause",       (Tcl_CmdProc*)Pause_cmd, NULL, NULL);
	Tcl_CreateCommand(interp, NS "::stop",        (Tcl_CmdProc*)Stop_cmd, NULL, NULL);
	Tcl_CreateCommand(interp, NS "::mci",         (Tcl_CmdProc*)Mci_cmd, NULL, NULL);

	return TCL_OK;
}