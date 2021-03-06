/***************************************************************************

    Konami 051649 - SCC1 sound as used in Haunted Castle, City Bomber

    This file is pieced together by Bryan McPhail from a combination of
    Namco Sound, Amuse by Cab, Haunted Castle schematics and whoever first
    figured out SCC!

    The 051649 is a 5 channel sound generator, each channel gets it's
    waveform from RAM (32 bytes per waveform, 8 bit signed data).

    This sound chip is the same as the sound chip in some Konami
    megaROM cartridges for the MSX. It is actually well researched
    and documented:

        http://www.msxnet.org/tech/scc

    Thanks to Sean Young (sean@mess.org) for some bugfixes.

    K052539 is equivalent to this chip except channel 5 does not share
    waveforms with channel 4.

***************************************************************************/

#include "burnint.h"
#include "k051649.h"

#define FREQBASEBITS	16

static UINT32 nUpdateStep;

/* this structure defines the parameters for a channel */
typedef struct
{
	UINT64 counter;
	INT32 frequency;
	INT32 volume;
	INT32 key;
	INT8 waveform[32];		/* 19991207.CAB */
} k051649_sound_channel;

typedef struct _k051649_state k051649_state;
struct _k051649_state
{
	k051649_sound_channel channel_list[5];

	/* global sound parameters */
	INT32 mclock,rate;
	double gain;
	INT32 output_dir;

	/* mixer tables and internal buffers */
	INT16 *mixer_table;
	INT16 *mixer_lookup;
	INT16 *mixer_buffer;
};

static k051649_state Chips[1]; // ok? (one is good enough)
static k051649_state *info;

/* build a table to divide by the number of voices */
static void make_mixer_table(INT32 voices)
{
	INT32 count = voices * 256;
	INT32 i;
	INT32 gain = 8;

	/* allocate memory */
	info->mixer_table = (INT16 *)BurnMalloc(512 * voices * sizeof(INT16));

	/* find the middle of the table */
	info->mixer_lookup = info->mixer_table + (256 * voices);

	/* fill in the table - 16 bit case */
	for (i = 0; i < count; i++)
	{
		INT32 val = i * gain * 16 / voices;
		if (val > 32767) val = 32767;
		info->mixer_lookup[ i] = val;
		info->mixer_lookup[-i] = -val;
	}
}


/* generate sound to the mix buffer */
void K051649Update(INT16 *pBuf, INT32 samples)
{
#if defined FBNEO_DEBUG
	if (!DebugSnd_K051649Initted) bprintf(PRINT_ERROR, _T("K051649Update called without init\n"));
#endif

	info = &Chips[0];
	k051649_sound_channel *voice=info->channel_list;
	INT16 *mix;
	INT32 i,v,j;
	double gain = info->gain;

	/* zap the contents of the mixer buffer */
	memset(info->mixer_buffer, 0, samples * sizeof(INT16));

	for (j=0; j<5; j++)
	{
		// channel is halted for freq < 9
		if (voice[j].frequency > 8)
		{
			const INT8 *w = voice[j].waveform;			/* 19991207.CAB */
			v=voice[j].volume * voice[j].key;
			INT32 c=voice[j].counter;
			INT32 step = (INT32)((((((float)info->mclock / (float)((voice[j].frequency+1) * 16))*(float)(1<<FREQBASEBITS)) / (float)(info->rate / 32)) * nUpdateStep) / 32768);
			mix = info->mixer_buffer;

			/* add our contribution */
			for (i = 0; i < samples; i++)
			{
				INT32 offs;

				c+= step;
				offs = (c >> 16) & 0x1f;
				*mix++ += ((w[offs] * v)>>3);
			}

			/* update the counter for this voice */
			voice[j].counter = c;
		}
	}

	/* mix it down */
	mix = info->mixer_buffer;
	for (i = 0; i < samples; i++) {
		INT32 output = info->mixer_lookup[*mix++];
		
		output = BURN_SND_CLIP(output);
		output = (INT32)(output * gain);
		output = BURN_SND_CLIP(output);
		
		INT32 nLeftSample = 0, nRightSample = 0;
		
		if ((info->output_dir & BURN_SND_ROUTE_LEFT) == BURN_SND_ROUTE_LEFT) {
			nLeftSample += output;
		}
		if ((info->output_dir & BURN_SND_ROUTE_RIGHT) == BURN_SND_ROUTE_RIGHT) {
			nRightSample += output;
		}

		pBuf[0] = BURN_SND_CLIP(pBuf[0] + nLeftSample);
		pBuf[1] = BURN_SND_CLIP(pBuf[1] + nRightSample);
		pBuf += 2;
	}
}

void K051649Init(INT32 clock)
{
	DebugSnd_K051649Initted = 1;

	info = &Chips[0];

	/* get stream channels */
	info->rate = clock/16;
	info->mclock = clock;
	info->gain = 1.00;
	info->output_dir = BURN_SND_ROUTE_BOTH;
	
	nUpdateStep = (INT32)(((float)info->rate / nBurnSoundRate) * 32768);

	/* allocate a buffer to mix into - 1 second's worth should be more than enough */
	info->mixer_buffer = (INT16 *)BurnMalloc(2 * sizeof(INT16) * info->rate);
	memset(info->mixer_buffer, 0, 2 * sizeof(INT16) * info->rate);
	
	/* build the mixer table */
	make_mixer_table(5);

	K051649Reset(); // clear things on init.
}

void K051649SetRoute(double nVolume, INT32 nRouteDir)
{
	info = &Chips[0];
	
	info->gain = nVolume;
	info->output_dir = nRouteDir;
}

void K051649Exit()
{
#if defined FBNEO_DEBUG
	if (!DebugSnd_K051649Initted) bprintf(PRINT_ERROR, _T("K051649Exit called without init\n"));
#endif

	if (!DebugSnd_K051649Initted) return;

	info = &Chips[0];

	BurnFree (info->mixer_buffer);
	BurnFree (info->mixer_table);
	
	nUpdateStep = 0;
	
	DebugSnd_K051649Initted = 0;
}

void K051649Reset()
{
#if defined FBNEO_DEBUG
	if (!DebugSnd_K051649Initted) bprintf(PRINT_ERROR, _T("K051649Reset called without init\n"));
#endif

	info = &Chips[0];
	k051649_sound_channel *voice = info->channel_list;
	INT32 i;

	/* reset all the voices */
	for (i = 0; i < 5; i++) {
		voice[i].frequency = 0;
		voice[i].volume = 0;
		voice[i].key = 0;
		voice[i].counter = 0;
		memset(&voice[i].waveform, 0, 32);
	}
}

void K051649Scan(INT32 nAction, INT32 *pnMin)
{
#if defined FBNEO_DEBUG
	if (!DebugSnd_K051649Initted) bprintf(PRINT_ERROR, _T("K051649Scan called without init\n"));
#endif

	struct BurnArea ba;

	if ((nAction & ACB_DRIVER_DATA) == 0) {
		return;
	}
	
	if (pnMin != NULL) {
		*pnMin = 0x029705;
	}

	memset(&ba, 0, sizeof(ba));
	ba.Data		= &info->channel_list;
	ba.nLen		= sizeof(k051649_sound_channel) * 5;
	ba.nAddress = 0;
	ba.szName	= "K051649 Channel list";
	BurnAcb(&ba);
}

/********************************************************************************/

void K051649WaveformWrite(INT32 offset, INT32 data)
{
#if defined FBNEO_DEBUG
	if (!DebugSnd_K051649Initted) bprintf(PRINT_ERROR, _T("K051649WaveformWrite called without init\n"));
#endif

	info = &Chips[0];
	info->channel_list[offset>>5].waveform[offset&0x1f]=data;
	/* SY 20001114: Channel 5 shares the waveform with channel 4 */
	if (offset >= 0x60)
		info->channel_list[4].waveform[offset&0x1f]=data;
}

UINT8 K051649WaveformRead(INT32 offset)
{
#if defined FBNEO_DEBUG
	if (!DebugSnd_K051649Initted) bprintf(PRINT_ERROR, _T("K051649WaveformRead called without init\n"));
#endif

	info = &Chips[0];
	return info->channel_list[offset>>5].waveform[offset&0x1f];
}

/* SY 20001114: Channel 5 doesn't share the waveform with channel 4 on this chip */
void K052539WaveformWrite(INT32 offset, INT32 data)
{
#if defined FBNEO_DEBUG
	if (!DebugSnd_K051649Initted) bprintf(PRINT_ERROR, _T("K052539WaveformWrite called without init\n"));
#endif

	info = &Chips[0];

	info->channel_list[offset>>5].waveform[offset&0x1f]=data;
}

void K051649VolumeWrite(INT32 offset, INT32 data)
{
#if defined FBNEO_DEBUG
	if (!DebugSnd_K051649Initted) bprintf(PRINT_ERROR, _T("K051649VolumeWrite called without init\n"));
#endif

	info = &Chips[0];

	info->channel_list[offset&0x7].volume=data&0xf;
}

void K051649FrequencyWrite(INT32 offset, INT32 data)
{
#if defined FBNEO_DEBUG
	if (!DebugSnd_K051649Initted) bprintf(PRINT_ERROR, _T("K051649FrequencyWrite called without init\n"));
#endif
	INT32 freq_hi = offset & 1;

	info = &Chips[0];

	if (info->channel_list[offset>>1].frequency < 9)
		info->channel_list[offset>>1].counter |= ((1 << FREQBASEBITS) - 1);

	// update frequency
	if (freq_hi)
		info->channel_list[offset>>1].frequency = (info->channel_list[offset>>1].frequency & 0x0ff) | (data << 8 & 0xf00);
	else
		info->channel_list[offset>>1].frequency = (info->channel_list[offset>>1].frequency & 0xf00) | data;
}

void K051649KeyonoffWrite(INT32 data)
{
#if defined FBNEO_DEBUG
	if (!DebugSnd_K051649Initted) bprintf(PRINT_ERROR, _T("K051649KeyonoffWrite called without init\n"));
#endif

	info = &Chips[0];
	info->channel_list[0].key=(data&1) ? 1 : 0;
	info->channel_list[1].key=(data&2) ? 1 : 0;
	info->channel_list[2].key=(data&4) ? 1 : 0;
	info->channel_list[3].key=(data&8) ? 1 : 0;
	info->channel_list[4].key=(data&16) ? 1 : 0;
}

