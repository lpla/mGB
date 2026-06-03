
void clampPresetSelect(UBYTE synth)
{
	x = synth + 24U;
	if(dataSet[x] >= PRESET_COUNT) dataSet[x] &= 0x0FU;
}

void clampDataValue(UBYTE p)
{
	l = tableData[p][2];
	if(l != 0xFFU && dataSet[p] >= l) dataSet[p] = l - 1U;
}

UBYTE getSaveDataChecksum(void)
{
	UBYTE checksum;
	checksum = SAVE_CHECKSUM_SEED;
	for(x=0U;x!=128U;x+=8U) {
		for(i=0U;i<7U;i++) checksum = (checksum << 1U) ^ saveData[x+i] ^ 0x11U;
		for(i=0U;i<6U;i++) checksum = (checksum << 1U) ^ saveData[128U+x+i] ^ 0x22U;
		for(i=0U;i<7U;i++) checksum = (checksum << 1U) ^ saveData[256U+x+i] ^ 0x44U;
		for(i=0U;i<4U;i++) checksum = (checksum << 1U) ^ saveData[384U+x+i] ^ 0x88U;
	}
	return checksum;
}

void writeDefaultSaveData(void)
{
	for(x=0U;x!=128U;x+=8U) {
		l=0U;
		for(i=0U;i<7U;i++) {
			saveData[(x+l)] = dataSet[i];
			l++;
		}
		l=0U;
		for(i=7U;i!=13U;i++) {
			saveData[(x+128U+l)] = dataSet[i];
			l++;
		}
		l=0U;
		for(i=13U;i!=20U;i++) {
			saveData[(x+256U+l)] = dataSet[i];
			l++;
		}
		l=0U;
		for(i=20U;i!=24U;i++) {
			saveData[(x+384U+l)] = dataSet[i];
			l++;
		}
	}
	saveData[SAVE_MAGIC_OFFSET] = SAVE_MAGIC;
	saveData[SAVE_CHECKSUM_OFFSET] = getSaveDataChecksum();
}

void saveDataSet(UBYTE synth)
{
	ENABLE_RAM_MBC1;
	clampPresetSelect(synth);
	x = (synth + 24U);
	x = dataSet[x] * PRESET_SLOT_SIZE;
	i=0;
	switch(synth)
		{
		case 0:
			for(j=0;j!=7;j++) {
				clampDataValue(j);
				saveData[x+i] = dataSet[j];
				i++;
			}
			break;
		case 1:
			for(j=7;j!=13;j++) {
				clampDataValue(j);
				saveData[128U+x+i] = dataSet[j];
				i++;
			}
			break;
		case 2:
			for(j=13;j!=20;j++) {
				clampDataValue(j);
				saveData[256U+x+i] = dataSet[j];
				i++;
			}
			break;
		case 3:
			for(j=20;j!=24;j++) {
				clampDataValue(j);
				saveData[384U+x+i] = dataSet[j];
				i++;
			}
			break;
		}
	saveData[SAVE_MAGIC_OFFSET] = SAVE_MAGIC;
	saveData[SAVE_CHECKSUM_OFFSET] = getSaveDataChecksum();
	DISABLE_RAM_MBC1;
}

void loadDataSet(UBYTE synth)
{
	ENABLE_RAM_MBC1;
	clampPresetSelect(synth);
	x = dataSet[(synth + 24U)] * PRESET_SLOT_SIZE;
	i=0;
	switch(synth)
		{
		case 0U:
			for(j=0;j!=7;j++) {
				if(!parameterLock[j]) {
					dataSet[j] = saveData[x+i];
					clampDataValue(j);
				}
				i++;
			}
			break;
		case 1U:
			for(j=7;j!=13;j++) {
				if(!parameterLock[j]) {
					dataSet[j] = saveData[128U+x+i];
					clampDataValue(j);
				}
				i++;
			}
			break;
		case 2U:
			for(j=13;j!=20;j++) {
				if(!parameterLock[j]) {
					dataSet[j] = saveData[256U+x+i];
					clampDataValue(j);
				}
				i++;
			}
			break;
		case 3U:
			j = 20;
			for(j=20;j!=24;j++) {
				if(!parameterLock[j]) {
					dataSet[j] = saveData[384U+x+i];
					clampDataValue(j);
				}
				i++;
			}
			break;
		}
	DISABLE_RAM_MBC1;
}

void snapRecall(void)
{
	if(cursorRowMain == 0x08U) {
		for(l=0;l<4;l++) {
			if(cursorEnable[l]){
				if(!recallMode) {
					saveDataSet(l);
				} else {
					loadDataSet(l);
					updateSynth(l);
				}
			}
		}
	} else {
		if(!recallMode) {
			for(j=0;j!=DATASET_SOUND_COUNT;j++) dataSetSnap[j] = dataSet[j];
		} else {
			for(j=0;j!=DATASET_SOUND_COUNT;j++) dataSet[j] = dataSetSnap[j];
			updateDisplay();
		}
	}
}

void checkMemory(void)
{
	ENABLE_RAM_MBC1;
	if(saveData[SAVE_MAGIC_OFFSET] != SAVE_MAGIC) {
		writeDefaultSaveData();
	} else {
		j = getSaveDataChecksum();
		if(saveData[SAVE_CHECKSUM_OFFSET] != j) {
			if(saveData[SAVE_CHECKSUM_OFFSET] == SAVE_CHECKSUM_EMPTY_A ||
				 saveData[SAVE_CHECKSUM_OFFSET] == SAVE_CHECKSUM_EMPTY_B) {
				saveData[SAVE_CHECKSUM_OFFSET] = j;
			} else {
				writeDefaultSaveData();
			}
		}
	}
	DISABLE_RAM_MBC1;

	for(j=0;j!=DATASET_SOUND_COUNT;j++) dataSetSnap[j] = dataSet[j];
}
