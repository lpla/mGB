UBYTE getCurrentNoteIndex(UBYTE synth)
{
	i = noteStatus[(synth << 1U) + 1U];
	if(i > NOTE_INDEX_MAX) i = NOTE_INDEX_MAX;
	return i;
}

UBYTE getPitchBendLowNoteIndex(UBYTE synth)
{
	i = getCurrentNoteIndex(synth);
	j = pbNoteRange[synth << 1U];
	if(j > i) return 0U;
	return j;
}

UBYTE getPitchBendHighNoteIndex(UBYTE synth)
{
	i = getCurrentNoteIndex(synth);
	j = pbNoteRange[(synth << 1U) + 1U];
	if(j < i || j > NOTE_INDEX_MAX) return NOTE_INDEX_MAX;
	return j;
}

void setPitchBendFrequencyOffset(UBYTE synth)
{
  UWORD freqRange;
  UWORD f;
	i = getCurrentNoteIndex(synth);
  f = freq[i];
  systemIdle = 0;
	if(pbWheelIn[synth] & 0x80) {
		freqRange = freq[getPitchBendHighNoteIndex(synth)];
		currentFreq = (UWORD) (pbWheelIn[synth] - 0x7F);
    currentFreq <<= 6;
		currentFreq /= 128;
		currentFreq = currentFreq * (freqRange - f);
		currentFreq = f + (currentFreq>>6);
	} else {
		freqRange = freq[getPitchBendLowNoteIndex(synth)];
    currentFreq = (UWORD) (0x80 - pbWheelIn[synth]);
    currentFreq <<= 6;
		currentFreq /= 128;
    currentFreq = currentFreq * (f - freqRange);
    currentFreq = f - (currentFreq>>6);
	}
	switch(synth)
		{
		case PU1:
			NR14_REG = (currentFreq>>8U);
			NR13_REG = currentFreq;
			currentFreqData[PU1] = currentFreq;
			break;
		case PU2:
			NR24_REG = (currentFreq>>8U);
			NR23_REG = currentFreq;
			currentFreqData[PU2] = currentFreq;
			break;
		default:
			NR34_REG = (currentFreq>>8U);
			NR33_REG = currentFreq;
			wavCurrentFreq = currentFreq;
			currentFreqData[WAV] = currentFreq;
		}
}

void setPitchBendFrequencyOffsetNoise(void)
{
  systemIdle = 0;
	i = noteStatus[NOI_CURRENT_NOTE];
	if(i > NOTE_INDEX_MAX) i = NOTE_INDEX_MAX;
  if(pbWheelIn[NOI] & 0x80) {
    j = ((pbWheelIn[NOI] - 0x80U) >>3U);
		if((NOTE_INDEX_MAX - i) < j) {
			i = NOTE_INDEX_MAX;
		} else {
			i += j;
		}
  } else {
    j = ((0x80U - pbWheelIn[NOI]) >>3U);
		if(j > i) {
			i = 0U;
		} else {
			i -= j;
		}
  }
	currentFreq = noiFreq[i];
  NR43_REG = currentFreq;
  currentFreqData[NOI] = currentFreq;
}

void addVibrato(UBYTE synth){
	if(vibratoDepth[synth]) {
		currentFreq = currentFreqData[synth] + vibratoPosition[synth];
		pbWheelInLast[synth] = PBWHEEL_CENTER;
		if(synth == PU1) {
			NR14_REG = (currentFreq>>8U);
			NR13_REG = currentFreq;
		} else if (synth == PU2) {
			NR24_REG = (currentFreq>>8U);
			NR23_REG = currentFreq;
		} else if (synth == WAV) {
			NR34_REG = (currentFreq>>8U);
			NR33_REG = currentFreq;
		} else {
			NR43_REG = currentFreq;
		}
	}
}

UBYTE vibratoTimer[4];

void updateVibratoPosition(UBYTE synth)
{
	if(vibratoTimer[synth] == vibratoSpeed[synth]) {
		vibratoTimer[synth] = 0x00;
		if(vibratoSlope[synth] && vibratoPosition[synth] < vibratoDepth[synth]) {
			vibratoPosition[synth]+=1;
		} else {
			vibratoSlope[synth] = 0;
			if(vibratoPosition[synth]) {
				vibratoPosition[synth]-=1;
			} else {
				vibratoSlope[synth]=1;
			}
		}
		addVibrato(synth);

	}
	vibratoTimer[synth]++;
}
