
#include <stdio.h>
#include <time.h>
#include <windows.h>

#include <MIDIClock.h>
#include <MIDIData.h>
#include <MIDIIO.h>
int main_out() {
  MIDIOut *pMIDIOut;
  char szDeviceName[32];
  unsigned char byMessage[3];
  long lRet;
  /* MIDI出力デバイス(No.0)の名前を調べる */
  lRet = MIDIOut_GetDeviceName(0, szDeviceName, 32);
  if (lRet == 0) {
    printf("利用できるMIDI出力デバイスはありません。\n");
    return 0;
  }
  /* MIDI出力デバイスを開く */
  pMIDIOut = MIDIOut_Open(szDeviceName);
  if (pMIDIOut == NULL) {
    printf("MIDI出力デバイス「%s」を開けません。\n", szDeviceName);
    return 0;
  }
  printf("MIDI出力デバイス「%s」を開きました。\n", szDeviceName);

  /* ドの音を発音する。 */
  byMessage[0] = 0x90;
  byMessage[1] = 0x3C;
  byMessage[2] = 0x64;
  lRet = MIDIOut_PutMIDIMessage(pMIDIOut, byMessage, 3);

  /* 10秒間消耗する。 */
  while (clock() < 10000) {
    Sleep(1);
  }

  /* ドの音を消音する。 */
  byMessage[0] = 0x90;
  byMessage[1] = 0x3C;
  byMessage[2] = 0x00;
  lRet = MIDIOut_PutMIDIMessage(pMIDIOut, byMessage, 3);

  /* MIDI出力デバイスを閉じる */
  MIDIOut_Close(pMIDIOut);
  return 1;
}

int main_smfload(const char *filepath) {
  char szBuf[1024];
  MIDIData *pMIDIData;
  MIDITrack *pMIDITrack;
  MIDIEvent *pMIDIEvent;
  MIDIOut *pMIDIOut;
  MIDIClock *pMIDIClock;
  char szDeviceName[32];
  unsigned char byMessage[3];
  long lRet;
  /* MIDI出力デバイス(No.0)の名前を調べる */
  lRet = MIDIOut_GetDeviceName(0, szDeviceName, 32);
  if (lRet == 0) {
    printf("利用できるMIDI出力デバイスはありません。\n");
    return 0;
  }
  /* MIDI出力デバイスを開く */
  pMIDIOut = MIDIOut_Open(szDeviceName);
  if (pMIDIOut == NULL) {
    printf("MIDI出力デバイス「%s」を開けません。\n", szDeviceName);
    return 0;
  }
  printf("MIDI出力デバイス「%s」を開きました。\n", szDeviceName);

  /* MIDIデータをスタンダードMIDIファイル(*.mid)から読み込む */
  pMIDIData = MIDIData_LoadFromSMF(filepath);
  if (pMIDIData == NULL) {
    printf("MIDIファイルが開けません。\n");
    return 0;
  }

  long tempo = -1;
  if (MIDIData_FindTempo(pMIDIData, MIDIData_GetBeginTime(pMIDIData), &tempo) ==
      0) {
    printf("tempo not found\n");
    return -1;
  }
  pMIDIClock = MIDIClock_Create(MIDIData_GetTimeMode(pMIDIData),
                                MIDIData_GetTimeResolution(pMIDIData), tempo);
  long timemode = MIDIData_GetTimeMode(pMIDIData);
  /* MIDIデータのプロパティを出力する。*/
  printf("[MIDIデータ]\n");
  printf("format=%d\n", MIDIData_GetFormat(pMIDIData));
  printf("track=%d\n", MIDIData_GetNumTrack(pMIDIData));
  printf("timemode=%d\n", timemode);
  printf("res=%d\n", MIDIData_GetTimeResolution(pMIDIData));
  printf("tempo=%d\n", tempo);

  int track_num = 0;
/* それぞれのトラックについて */
#define MAX_TRACK 32
  MIDIEvent *events[MAX_TRACK];
  BOOL tr_end[MAX_TRACK] = {FALSE};
  {
    MIDITrack *tr = MIDIData_GetFirstTrack(pMIDIData);
    for (int i = 0; i < MAX_TRACK; i++) {
      if(!tr) break;
      events[i] = MIDITrack_GetFirstEvent(tr);
      track_num++;
      tr = MIDITrack_GetNextTrack(tr);
    }
  }
  int tr_end_cnt;

  MIDIClock_Reset(pMIDIClock);
  MIDIClock_Start(pMIDIClock);
  while (1) {
    tr_end_cnt = 0;
    long current_time;
    if (timemode == MIDIDATA_TPQNBASE) {
      current_time = MIDIClock_GetTickCount(pMIDIClock);
    } else {
      current_time = MIDIClock_GetMillisec(pMIDIClock);
    }
    for (int i = 0; i < track_num; i++) {
      if (!tr_end[i]) {
        while (1) {
          long evt_time = MIDIEvent_GetTime(events[i]);
          if (evt_time > current_time) {
            break;
          }
          // printf("%s\n", MIDIEvent_ToString(events[i], szBuf, sizeof(szBuf)));

          if (MIDIEvent_IsTempo(events[i])) {
            printf("tempo: %s\n",
                   MIDIEvent_ToString(events[i], szBuf, sizeof(szBuf)));
            MIDIClock_SetTempo(pMIDIClock, MIDIEvent_GetTempo(events[i]));

          } else if (!MIDIEvent_IsMetaEvent(events[i])) {
            if (MIDIOut_PutMIDIMessage(pMIDIOut, events[i]->m_pData,
                                       events[i]->m_lLen) == 0) {
              printf("%s\n",
                     MIDIEvent_ToString(events[i], szBuf, sizeof(szBuf)));
            }
          }

          events[i] = MIDIEvent_GetNextEvent(events[i]);
          if (!events[i]) {
            tr_end[i] = TRUE;
            break;
          }
        }
      } else {
        tr_end_cnt++;
      }
    }
    if (tr_end_cnt >= track_num) {
      break;
    }

    Sleep(1);
  }
  MIDIClock_Stop(pMIDIClock);
  // forEachTrack (pMIDIData, pMIDITrack) {
  // 	printf ("[MIDI Track]\n");
  // 	printf ("sho:haku:tick type              len  内容\n");
  // 	/* それぞれのイベントについて */
  // 	forEachEvent (pMIDITrack, pMIDIEvent) {
  // 		printf ("%s\n", MIDIEvent_ToString (pMIDIEvent, szBuf,
  // sizeof(szBuf)));

  // 		MIDIOut_PutMIDIMessage (pMIDIOut, pMIDIEvent->m_pData,
  // pMIDIEvent->m_lLen);
  // 		Sleep(100);
  // 	}
  // }
  MIDIClock_Delete(pMIDIClock);
  /* MIDIデータをメモリ上から削除する */
  MIDIData_Delete(pMIDIData);

  /* MIDI出力デバイスを閉じる */
  MIDIOut_Close(pMIDIOut);
}

int main(int argc, char *argv[]) {
  if (argc < 3) {
    printf("usage: %s MIDI_FILE\n", argv[0]);
  }
  main_smfload(argv[1]);
}