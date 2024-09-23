/******************************************************************************/
/*                                                                            */
/*  MIDIIO.c - MIDI入出力用モジュール(Win32用)             (C)2002-2021 くず  */
/*                                                                            */
/******************************************************************************/

/* このモジュールは普通のＣ言語で書かれている。 */
/* このライブラリは、GNU 劣等一般公衆利用許諾契約書(LGPL)に基づき配布される。 */
/* プロジェクトホームページ(仮)："http://openmidiproject.sourceforge.jp/index.html" */
/* MIDI入力オブジェクトのオープン・クローズ・リセット・データ受信(SYSX含む) */
/* MIDI出力オブジェクトのオープン・クローズ・リセット・データ送信(SYSX含む) */

/* This library is free software; you can redistribute it and/or */
/* modify it under the terms of the GNU Lesser General Public */
/* License as published by the Free Software Foundation; either */
/* version 2.1 of the License, or (at your option) any later version. */

/* This library is distributed in the hope that it will be useful, */
/* but WITHOUT ANY WARRANTY; without even the implied warranty of */
/* MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU */
/* Lesser General Public License for more details. */

/* You should have received a copy of the GNU Lesser General Public */
/* License along with this library; if not, write to the Free Software */
/* Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA */

#include <stdio.h>
#include <tchar.h>
#include <windows.h>
#include <crtdbg.h>
#include <mmsystem.h>
#include "MIDIIO.h"

#ifndef _MSC_VER
#define strcpy_s(str1, str1len, str2) {strncpy(str1,str2,str1len); ((char*)str1)[str1len-1]='\0';}
#define strncpy_s(str1, str1len, str2, str2len) strcpy_s(str1,(str1len<(str2len+1))?str1len:(str2len+1), str2)
#define wcsncpy_s(str1, str1len, str2, str2len) wcsncpy(str1,(str1len<(str2len+1))?str1len:(str2len+1), str2)
#endif

/* 汎用マクロ(最小、最大、挟み込み) ******************************************/
#ifndef MIN
#define MIN(A,B) ((A)>(B)?(B):(A))
#endif
#ifndef MAX
#define MAX(A,B) ((A)>(B)?(A):(B))
#endif
#ifndef CLIP
#define CLIP(A,B,C) ((A)>(B)?(A):((B)>(C)?(C):(B)))
#endif

/* WSIZEOFマクロ **************************************************************/
#define WSIZEOF(STRING) (sizeof(STRING)/sizeof(wchar_t))

/* 汎用関数 ********************************************************************/

/* size_tをlongに変換 */
static long size_ttolong (size_t n) {
	if (n > LONG_MAX) {
		return LONG_MAX;
	}
	else if (n < 0) {
		return 0;
	}
	return (long)n;
}

/* INT_PTRをlongに変換 */
static long INT_PTRtolong (INT_PTR n) {
	if (n > LONG_MAX) {
		return LONG_MAX;
	}
	else if (n < LONG_MIN) {
		return LONG_MIN;
	}
	return (long)n;
}

/* DLLMain　*******************************************************************/
static BOOL WINAPI DllMain (HINSTANCE hinstDLL, DWORD fdwReason, LPVOID lpvReserved) {
	/* Windows10対策 */
	switch (fdwReason) {
	case DLL_PROCESS_ATTACH:
		/*20210608 Windows10 Microsoft Print to PDFで印刷しようとすると固まるので廃止 */
		/* CoInitializeEx (NULL, COINIT_MULTITHREADED); */
		break;
	case DLL_PROCESS_DETACH:
		/* CoUninitialize (); */
		break;
	}
	return TRUE;
}

/* MIDI入出力両用関数 *********************************************************/

/* バッファに対して簡易クリティカルセクションを張る */
static void MIDIIO_LockBuf (MIDI* pMIDI) {
	_ASSERT (pMIDI);
	while (pMIDI->m_bBufLocked) {
		Sleep (1);
	}
	pMIDI->m_bBufLocked = 1;
}

/* バッファに対して簡易クリティカルセクションを抜ける */
static void MIDIIO_UnLockBuf (MIDI* pMIDI) {
	_ASSERT (pMIDI);
	pMIDI->m_bBufLocked = 0;
}

/* メモリブロックをテキスト表現する */
static void MIDIIO_Bin2Txt 
(unsigned char* pBinData, long lBinDataLen, char* pTextData, long lTextDataLen) {
	char szMsg1[256];
	int i;
	int iMax = MIN (lBinDataLen, lTextDataLen / 3);
	memset (pTextData, 0, lTextDataLen);
	for (i = 0; i < iMax; i++) {
		sprintf_s (szMsg1, sizeof (szMsg1), "%02X", *(pBinData + i));
		strcat_s (pTextData, lTextDataLen, szMsg1);
		if (i != iMax - 1) {
			strcat_s (pTextData, lTextDataLen, " ");
		}
	}
}

/******************************************************************************/
/*                                                                            */
/*  MIDIOutクラス関数群                                                       */
/*                                                                            */
/******************************************************************************/

/* MIDI出力デバイスが開かれたときに呼び出される。(隠蔽) */
static void MIDIOut_OnMOMOpen 
(HMIDIOUT hMIDIOut, DWORD_PTR dwInstance, DWORD_PTR dwParam1, DWORD_PTR dwParam2) {
	;
}

/* MIDI出力デバイスが閉じられたときに呼び出される。(隠蔽) */
static void MIDIOut_OnMOMClose 
(HMIDIOUT hMIDIOut, DWORD_PTR dwInstance, DWORD_PTR dwParam1, DWORD_PTR dwParam2) {
	;
}

/* SYSXの送信が終了したときに呼び出される。(隠蔽) */
static void MIDIOut_OnMOMDone 
(HMIDIOUT hMIDIOut, DWORD_PTR dwInstance, DWORD_PTR dwParam1, DWORD_PTR dwParam2) {
	MIDIHDR* pMIDIHdr = (MIDIHDR*)(dwParam1);
	MIDIOut* pMIDIOut = (MIDIOut*)(dwInstance);
	int i;
	int nRet;
	if (pMIDIOut == NULL) {
		return;
	}
	/* 何番目のSYSXヘッダーが送信完了したか検出する */
	for (i = 0; i < MIDIIO_SYSXNUM; i++) {
		if (pMIDIOut->m_pSysxHeader[i] == pMIDIHdr) {
			break;
		}
	}
	if (i >= MIDIIO_SYSXNUM) {
		_RPTF0 (_CRT_WARN, "警告：MIDIOut_OnMOMDone内で出所不明なSysxデータを検出しました。\r\n");
		return;
	}
	/* 使用済みSYSXヘッダーの不準備(20070106) */
	nRet = midiOutUnprepareHeader 
		(pMIDIOut->m_pDeviceHandle, ((MIDIHDR*)(pMIDIOut->m_pSysxHeader[i])), sizeof(MIDIHDR));
	if (nRet != MMSYSERR_NOERROR) {
		_RPTF1 (_CRT_WARN, "エラー：MIDIOut_OnMOMDone内でmidiOutUnprepareHeaderが%dを返しました。\r\n", nRet);
	}
	/* 使用済みSYSXヘッダーの解放(20070520) */
	HeapFree (GetProcessHeap (), 0, ((MIDIHDR*)(pMIDIOut->m_pSysxHeader[i]))->lpData);
	HeapFree (GetProcessHeap (), 0, (MIDIHDR*)(pMIDIOut->m_pSysxHeader[i]));
	pMIDIOut->m_pSysxHeader[i] = NULL;
	
	/* TODO 本来コールバック関数内でシステムコールは禁止であるが、 */
	/* 上記midiOutUnprepareHeaderとHeapFreeに限り、今のところ問題は発生していない。 */
}

/* MIDI出力デバイスからメッセージを受け取るコールバック関数。(隠蔽) */
static void CALLBACK MidiOutProc (HMIDIOUT hMIDIOut, UINT wMsg,         
  DWORD_PTR dwInstance, DWORD_PTR dwParam1, DWORD_PTR dwParam2) {
	switch (wMsg) {
	case MOM_OPEN:
		_RPTF3 (_CRT_WARN, "MOM_OPEN, 0x%p, 0x%p, 0x%p\r\n", 
			dwInstance, dwParam1, dwParam2);
		MIDIOut_OnMOMOpen (hMIDIOut, dwInstance, dwParam1, dwParam2);
		break;
	case MOM_CLOSE:
		_RPTF3 (_CRT_WARN, "MOM_CLOSE, 0x%p, 0x%p, 0x%p\r\n", 
			dwInstance, dwParam1, dwParam2);
		MIDIOut_OnMOMClose (hMIDIOut, dwInstance, dwParam1, dwParam2);
		break;
	case MOM_DONE:
		_RPTF3 (_CRT_WARN, "MOM_DONE, 0x%p, 0x%p, 0x%p\r\n", 
			dwInstance, dwParam1, dwParam2);
		MIDIOut_OnMOMDone (hMIDIOut, dwInstance, dwParam1, dwParam2);
		break;
	}
}


/* MIDI出力デバイスの数を調べる */
long __stdcall MIDIOut_GetDeviceNum () {
	return midiOutGetNumDevs ();
}

/* MIDI出力デバイスの名前を調べる(ANSI) */
long __stdcall MIDIOut_GetDeviceNameA (long lID, char* pszDeviceName, long lLen) {
	int nRet;
	long lLen2;
	MIDIOUTCAPSA tagMIDIOutCaps;
	memset (&tagMIDIOutCaps, 0, sizeof (MIDIOUTCAPSA));
	nRet = midiOutGetDevCapsA (lID, &tagMIDIOutCaps, sizeof(MIDIOUTCAPSA));
	if (nRet != MMSYSERR_NOERROR) {
		return 0;
	}
	memset (pszDeviceName, 0, lLen);
	lLen2 = size_ttolong (strlen (tagMIDIOutCaps.szPname));
	strncpy_s (pszDeviceName, lLen, tagMIDIOutCaps.szPname, MIN (lLen - 1, lLen2));
	return MIN (lLen - 1, lLen2);
}

/* MIDI出力デバイスの名前を調べる(UNICODE) */
long __stdcall MIDIOut_GetDeviceNameW (long lID, wchar_t* pszDeviceName, long lLen) {
	int nRet;
	long lLen2;
	MIDIOUTCAPSW tagMIDIOutCaps;
	memset (&tagMIDIOutCaps, 0, sizeof (MIDIOUTCAPSW));
	nRet = midiOutGetDevCapsW (lID, &tagMIDIOutCaps, sizeof(MIDIOUTCAPSW));
	if (nRet != MMSYSERR_NOERROR) {
		return 0;
	}
	memset (pszDeviceName, 0, lLen * sizeof (wchar_t));
	lLen2 = size_ttolong (wcslen (tagMIDIOutCaps.szPname));
	wcsncpy_s (pszDeviceName, lLen, tagMIDIOutCaps.szPname, MIN (lLen - 1, lLen2));
	return MIN (lLen - 1, lLen2);
}


/* MIDI出力デバイスを開く(ANSI) */
MIDIOut* __stdcall MIDIOut_OpenA (const char* pszDeviceName) {
	MIDIOUTCAPSA tagMIDIOutCaps;
	MIDI* pMIDIOut;
	long lDeviceNameLen;
	int nNumDevices, i, nRet;
	if (pszDeviceName == NULL) {
		return NULL;
	}
	/* デバイス名が空文字列又は"None"又は"なし"の場合は何も開かない */
	if (*pszDeviceName == 0) {
		return NULL;
	}
	else if (strcmp (pszDeviceName, MIDIIO_NONE) == 0 ||
		strcmp (pszDeviceName, MIDIIO_NONEJ) == 0) {
		return NULL;
	}
	/* デバイス名が"default"か"デフォルト"か"MIDI Mapper"か"MIDI マッパー"である場合は */
	/* 強制的にMIDIマッパー(-1)とする。 */
	else if (strcmp (pszDeviceName, MIDIIO_DEFAULT) == 0 ||
		strcmp (pszDeviceName, MIDIIO_DEFAULTJ) == 0 ||
		strcmp (pszDeviceName, MIDIIO_MIDIMAPPER) == 0 ||
		strcmp (pszDeviceName, MIDIIO_MIDIMAPPERJ) == 0) {
		i = -1;
		strncpy_s (tagMIDIOutCaps.szPname, 32, pszDeviceName, 31);
	}
	/* デバイス名を解析し何番目のMIDI出力デバイスか判定 */
	else {
		nNumDevices = midiOutGetNumDevs ();
		for (i = 0; i < nNumDevices; i++) {
			memset (&tagMIDIOutCaps, 0, sizeof (MIDIOUTCAPSA));
			nRet = midiOutGetDevCapsA (i, &tagMIDIOutCaps, sizeof(MIDIOUTCAPSA));
			if (strncmp (pszDeviceName, tagMIDIOutCaps.szPname, 32) == 0) {
				break;
			}
		}
		if (i >= nNumDevices) {
			_RPTF1 (_CRT_WARN, "エラー：MIDIOut_OpenA内でMIDI出力デバイスが見つかりません。-%s\r\n", pszDeviceName);
			return NULL;
		}
	}
	/* MIDIOut構造体の確保 */
	pMIDIOut = calloc (1, sizeof (MIDI));
	if (pMIDIOut == NULL) {
		_RPTF0 (_CRT_WARN, "エラー：MIDIOut_OpenA内でcallocがNULLを返しました。\r\n");
		return NULL;
	}
	lDeviceNameLen = size_ttolong (strlen (pszDeviceName) + 1);
	pMIDIOut->m_pDeviceName = calloc (lDeviceNameLen, sizeof (char));
	if (pMIDIOut->m_pDeviceName == NULL) {
		_RPTF0 (_CRT_WARN, "エラー：MIDIOut_OpenA内でcallocがNULLを返しました。\r\n");
		return NULL;
	}
	strncpy_s ((char*)(pMIDIOut->m_pDeviceName), lDeviceNameLen, pszDeviceName, lDeviceNameLen - 1);
	pMIDIOut->m_lMode = MIDIIO_MODEOUT;
	/* MIDI出力デバイスのオープン */
	nRet = midiOutOpen ((HMIDIOUT*)&(pMIDIOut->m_pDeviceHandle), i, 
		(DWORD_PTR)MidiOutProc, (DWORD_PTR)pMIDIOut, CALLBACK_FUNCTION);
	if (nRet != 0) {
		free (pMIDIOut->m_pBuf);
		free (pMIDIOut);
		_RPTF1 (_CRT_WARN, "エラー：MIDIOut_OpenA内でmidiOutOpenが%dを返しました。\r\n", nRet);
		return NULL;
	}
	/* SYSXヘッダーの領域設定(20070520) */
	for (i = 0; i < MIDIIO_SYSXNUM; i++) {
		pMIDIOut->m_pSysxHeader[i] = NULL;
	}
	return pMIDIOut;
}

/* MIDI出力デバイスを開く(UNICODE) */
MIDIOut* __stdcall MIDIOut_OpenW (const wchar_t* pszDeviceName) {
	MIDIOUTCAPSW tagMIDIOutCaps;
	MIDI* pMIDIOut;
	long lDeviceNameLen;
	int nNumDevices, i, nRet;
	if (pszDeviceName == NULL) {
		return NULL;
	}
	/* デバイス名が空文字列又は"None"又は"なし"の場合は何も開かない */
	if (*pszDeviceName == 0) {
		return NULL;
	}
	else if (wcscmp (pszDeviceName, L"(None)") == 0 ||
		wcscmp (pszDeviceName, L"(なし)") == 0) {
		return NULL;
	}
	/* デバイス名が"default"か"デフォルト"か"MIDI Mapper"か"MIDI マッパー"である場合は */
	/* 強制的にMIDIマッパー(-1)とする。 */
	else if (wcscmp (pszDeviceName, L"Default") == 0 ||
		wcscmp (pszDeviceName, L"デフォルト") == 0 ||
		wcscmp (pszDeviceName, L"MIDI Mapper") == 0 ||
		wcscmp (pszDeviceName, L"MIDI マッパー") == 0) {
		i = -1;
		wcsncpy_s (tagMIDIOutCaps.szPname, 32, pszDeviceName, 31);
	}
	/* デバイス名を解析し何番目のMIDI出力デバイスか判定 */
	else {
		nNumDevices = midiOutGetNumDevs ();
		for (i = 0; i < nNumDevices; i++) {
			memset (&tagMIDIOutCaps, 0, sizeof (MIDIOUTCAPSW));
			nRet = midiOutGetDevCapsW (i, &tagMIDIOutCaps, sizeof(MIDIOUTCAPSW));
			if (wcsncmp (pszDeviceName, tagMIDIOutCaps.szPname, 32) == 0) {
				break;
			}
		}
		if (i >= nNumDevices) {
			_RPTF1 (_CRT_WARN, "エラー：MIDIOut_OpenW内でMIDI出力デバイスが見つかりません。-%s\r\n", pszDeviceName);
			return NULL;
		}
	}
	/* MIDIOut構造体の確保 */
	pMIDIOut = calloc (1, sizeof (MIDI));
	if (pMIDIOut == NULL) {
		_RPTF0 (_CRT_WARN, "エラー：MIDIOut_OpenW内でcallocがNULLを返しました。\r\n");
		return NULL;
	}
	lDeviceNameLen = size_ttolong (wcslen (pszDeviceName) + 1);
	pMIDIOut->m_pDeviceName = calloc (lDeviceNameLen, sizeof (wchar_t));
	if (pMIDIOut->m_pDeviceName == NULL) {
		_RPTF0 (_CRT_WARN, "エラー：MIDIOut_OpenW内でcallocがNULLを返しました。\r\n");
		return NULL;
	}
	wcsncpy_s ((wchar_t*)(pMIDIOut->m_pDeviceName), lDeviceNameLen, pszDeviceName, lDeviceNameLen - 1);
	pMIDIOut->m_lMode = MIDIIO_MODEOUT;
	/* MIDI出力デバイスのオープン */
	nRet = midiOutOpen ((HMIDIOUT*)&(pMIDIOut->m_pDeviceHandle), i, 
		(DWORD_PTR)MidiOutProc, (DWORD_PTR)pMIDIOut, CALLBACK_FUNCTION);
	if (nRet != 0) {
		free (pMIDIOut->m_pBuf);
		free (pMIDIOut);
		_RPTF1 (_CRT_WARN, "エラー：MIDIOut_OpenW内でmidiOutOpenが%dを返しました。\r\n", nRet);
		return NULL;
	}
	/* SYSXヘッダーの領域設定(20070520) */
	for (i = 0; i < MIDIIO_SYSXNUM; i++) {
		pMIDIOut->m_pSysxHeader[i] = NULL;
	}
	return pMIDIOut;
}


/* MIDI出力デバイスを閉じる */
long __stdcall MIDIOut_Close (MIDIOut* pMIDIOut) {
	int nRet;
	int i, t;
	if (pMIDIOut == NULL) {
		return 1;
	}
	_ASSERT (pMIDIOut->m_lMode == MIDIIO_MODEOUT);
	/* MIDI出力リセット */
	midiOutReset (pMIDIOut->m_pDeviceHandle);
	/* すべてのSYSXヘッダーに対してMOM_DONEを待つ(100回試行) */
	for (t = 0; t < 100; t++) {
		for (i = 0; i < MIDIIO_SYSXNUM; i++) {
			if (pMIDIOut->m_pSysxHeader[i] != NULL) {
				break;
			}
		}
		if (i >= MIDIIO_SYSXNUM) {
			break;
		}
		Sleep (10);
	}
	if (t == 100) {
		_RPTF0 (_CRT_WARN, "警告：MIDIOut_Close内でMOM_DONEがコールバックされていないバッファがあります。\r\n");
	}
	/* MIDI出力デバイスを閉じる */
	nRet = midiOutClose (pMIDIOut->m_pDeviceHandle);
	if (nRet != MMSYSERR_NOERROR) {
		_RPTF1 (_CRT_WARN, "エラー：MIDIOut_Close内でmidiOutCloseが%dを返しました。\r\n", nRet);
		return 0;
	}
	/* このオブジェクトの解放 */
	free (pMIDIOut->m_pBuf);
	pMIDIOut->m_pBuf = NULL;
	free (pMIDIOut->m_pDeviceName);
	pMIDIOut->m_pDeviceName = NULL;
	free (pMIDIOut);
	return 1;
}

/* MIDI出力デバイスを再び開く(ANSI) */
MIDIOut* __stdcall MIDIOut_ReopenA (MIDIOut* pMIDIOut, const char* pszDeviceName) {
	int nRet = 0;
	if (pMIDIOut) {
		_ASSERT (pMIDIOut->m_lMode == MIDIIO_MODEOUT);
		nRet = MIDIOut_Close (pMIDIOut);
		if (nRet == 0) {
			_RPTF1 (_CRT_WARN, "エラー：MIDIOut_ReopenA内でMIDIOut_Closeが%dを返しました。\r\n", nRet);
			return NULL;
		}
	}
	return MIDIOut_OpenA (pszDeviceName);
}

/* MIDI出力デバイスを再び開く(UNICODE) */
MIDIOut* __stdcall MIDIOut_ReopenW (MIDIOut* pMIDIOut, const wchar_t* pszDeviceName) {
	int nRet = 0;
	if (pMIDIOut) {
		_ASSERT (pMIDIOut->m_lMode == MIDIIO_MODEOUT);
		nRet = MIDIOut_Close (pMIDIOut);
		if (nRet == 0) {
			_RPTF1 (_CRT_WARN, "エラー：MIDIOut_ReopenW内でMIDIOut_Closeが%dを返しました。\r\n", nRet);
			return NULL;
		}
	}
	return MIDIOut_OpenW (pszDeviceName);
}

/* MIDI出力デバイスをリセットする */
long __stdcall MIDIOut_Reset (MIDIOut* pMIDIOut) {
	int nRet = 0;
	_ASSERT (pMIDIOut);
	_ASSERT (pMIDIOut->m_lMode == MIDIIO_MODEOUT);
	nRet = midiOutReset (pMIDIOut->m_pDeviceHandle);
	if (nRet != MMSYSERR_NOERROR) {
		_RPTF1 (_CRT_WARN, "エラー：MIDIOut_Reset内でmidiOutResetが%dを返しました。\r\n", nRet);
		return 0;
	}
	return 1;
}

/* MIDI出力デバイスにMIDIメッセージを1つ出力する */
long __stdcall MIDIOut_PutMIDIMessage (MIDIOut* pMIDIOut, unsigned char* pMessage, long lLen) {
	_ASSERT (pMIDIOut);
	_ASSERT (pMIDIOut->m_lMode == MIDIIO_MODEOUT);
	_ASSERT (pMessage);
	_ASSERT (1 <= lLen && lLen <= MIDIIO_MAXSYSXSIZE);
	/* システムエクスクルーシヴメッセージ */
	if (lLen >= 1 && *pMessage == 0xF0) {
		int nRet;
		int i;
		/* 空いているSysxHeaderを探す。 */
		for (i = 0; i < MIDIIO_SYSXNUM; i++) {
			if (pMIDIOut->m_pSysxHeader[i] == NULL) {
				break;
			}
		}
		if (i >= MIDIIO_SYSXNUM) {
			_RPTF0 (_CRT_WARN, "エラー：MIDIOut_PutMIDIMessageで使用可能なSYSXヘッダーがもはやありません。\r\n");
			return 0;
		}
		/* SysxHeaderのバッファ確保 */

		pMIDIOut->m_pSysxHeader[i] =
			(MIDIHDR*)HeapAlloc(GetProcessHeap (), HEAP_ZERO_MEMORY, sizeof (MIDIHDR));
		if ((MIDIHDR*)(pMIDIOut->m_pSysxHeader[i]) == NULL) {
			_RPTF0 (_CRT_WARN, "エラー：MIDIOut_PutMIDIMessageでHeapAllocがNULLを返しました。\r\n");
			return 0;
		}
		((MIDIHDR*)(pMIDIOut->m_pSysxHeader[i]))->lpData = 
			(char*)HeapAlloc (GetProcessHeap (), HEAP_NO_SERIALIZE, MIN (lLen, MIDIIO_MAXSYSXSIZE));
		if (((MIDIHDR*)(pMIDIOut->m_pSysxHeader[i]))->lpData == NULL) {
			_RPTF0 (_CRT_WARN, "エラー：MIDIOut_PutMIDIMessageでHeapAllocがNULLを返しました。\r\n");
			return 0;
		}
		/* SYSXヘッダー内部のデータ用領域にMIDIメッセージをコピー */
		((MIDIHDR*)(pMIDIOut->m_pSysxHeader[i]))->dwBufferLength = MIN (lLen, MIDIIO_MAXSYSXSIZE);
		memcpy (((MIDIHDR*)(pMIDIOut->m_pSysxHeader[i]))->lpData, pMessage, MIN (lLen, MIDIIO_MAXSYSXSIZE));
		((MIDIHDR*)(pMIDIOut->m_pSysxHeader[i]))->dwBytesRecorded = MIN (lLen, MIDIIO_MAXSYSXSIZE);
		/* midiOutPrepareHeader */
		nRet = midiOutPrepareHeader 
			(pMIDIOut->m_pDeviceHandle, ((MIDIHDR*)(pMIDIOut->m_pSysxHeader[i])), sizeof (MIDIHDR));
		if (nRet != MMSYSERR_NOERROR) {
			_RPTF1 (_CRT_WARN, "エラー：MIDIOut_PutMIDIMessage内でmidiOutPrepareHeaderが%dを返しました。\r\n", nRet);
			return 0;
		}
		/* midiOutLongMsg...この関数実行直後かやや後にMOM_DONEがコールバックされる */
		nRet = midiOutLongMsg 
			(pMIDIOut->m_pDeviceHandle, ((MIDIHDR*)(pMIDIOut->m_pSysxHeader[i])), sizeof(MIDIHDR));
		if (nRet != MMSYSERR_NOERROR) {
			_RPTF1 (_CRT_WARN, "エラー：MIDIOut_PutMIDIMessage内でmidiOutLongMsgが%dを返しました。\r\n", nRet);
			nRet = midiOutUnprepareHeader 
				(pMIDIOut->m_pDeviceHandle, ((MIDIHDR*)(pMIDIOut->m_pSysxHeader[i])), sizeof (MIDIHDR));
			HeapFree (GetProcessHeap (), 0, ((MIDIHDR*)(pMIDIOut->m_pSysxHeader[i]))->lpData);
			HeapFree (GetProcessHeap (), 0, ((MIDIHDR*)(pMIDIOut->m_pSysxHeader[i])));
			pMIDIOut->m_pSysxHeader[i] = NULL;
			return 0;
		}
		return lLen;
	}
	/* 通常のMIDIメッセージ又はシステムリアルタイムメッセージ又はシステムコモンメッセージ */
	else if (1 <= lLen && lLen <= 3) {
		unsigned long lMsg;
		switch (lLen) {
		case 1:
			lMsg = *(pMessage) ;
			break;
		case 2:
			lMsg = *(pMessage) + (*(pMessage + 1) << 8);
			break;
		case 3:
			lMsg = *(pMessage) + (*(pMessage + 1) << 8) + (*(pMessage + 2) << 16);
			break;
		}
		midiOutShortMsg (pMIDIOut->m_pDeviceHandle, lMsg);
		return lLen;
	}
	/* 異常なMIDIメッセージ */
	else {
		_RPTF0 (_CRT_WARN, "エラー：MIDIOut_PutMIDIMessage内で異常なMIDIメッセージが検出されました。\r\n");
		return 0;
	}
}

/* MIDI出力デバイスに1バイト出力する */
long __stdcall MIDIOut_PutByte (MIDIOut* pMIDIOut, unsigned char c) {
	_ASSERT (pMIDIOut);
	_ASSERT (pMIDIOut->m_lMode == MIDIIO_MODEOUT);
	midiOutShortMsg (pMIDIOut->m_pDeviceHandle, (unsigned long)c);
	return 1;
}

/* MIDI出力デバイスに任意長のデータを出力する */
long __stdcall MIDIOut_PutBytes (MIDIOut* pMIDIOut, unsigned char* pData, long lLen) {
	unsigned char cRunningStatus = 0;
	long lRet = 0;
	long lLen2 = 0;
	unsigned char* p1 = pData;
	_ASSERT (pMIDIOut);
	_ASSERT (pMIDIOut->m_lMode == MIDIIO_MODEOUT);
	if (pMIDIOut == NULL) {
		return 0;
	}
	if (pMIDIOut->m_lMode != MIDIIO_MODEOUT) {
		return 0;
	}
	while (p1 < pData + lLen) {
		if (*p1 < 0x80) {
			if (0x80 <= cRunningStatus && cRunningStatus <= 0xBF ||
				0xE0 <= cRunningStatus && cRunningStatus <= 0xEF) { 
				lLen2 = MIN (2, INT_PTRtolong (pData + lLen - p1));
				lRet += MIDIOut_PutMIDIMessage (pMIDIOut, p1, lLen2);
				p1 += lLen2;
			}
			else if (0xC0 <= cRunningStatus && cRunningStatus <= 0xDF) {
				lLen2 = MIN (1, INT_PTRtolong (pData + lLen - p1));
				lRet += MIDIOut_PutMIDIMessage (pMIDIOut, p1, lLen2);
				p1 += lLen2;
			}
			else if (cRunningStatus == 0xF0) {
				char* p2 = p1;
				lLen2 = 0;
				cRunningStatus = 0xF0;
				while (*p2++ != 0xF7 && lLen2 < MIDIIO_MAXSYSXSIZE) {
					lLen2++;
				}	
				lRet += MIDIOut_PutMIDIMessage (pMIDIOut, p1, (lLen2 + 1));
				p1 += (lLen2 + 1);
			}
			else {
				while (*p1 < 0x80 || p1 < pData + lLen) {
					p1++;	
				}
				lLen2 = 0;
				_RPTF0 (_CRT_WARN, "エラー：MIDIOut_PutBytes内で異常なMIDIメッセージを検出しました。\r\n");
			}
		}
		else if (*p1 == 0xF0) {
			char* p2 = p1;
			lLen2 = 0;
			cRunningStatus = 0xF0;
			while (*p2++ != 0xF7 && lLen2 < MIDIIO_MAXSYSXSIZE) {
				lLen2++;
			}
			lRet += MIDIOut_PutMIDIMessage (pMIDIOut, p1, (lLen2 + 1));
			p1 += (lLen2 + 1);
		}
		else {
			if (0x80 <= *p1 && *p1 <= 0xBF ||
				0xE0 <= *p1 && *p1 <= 0xEF || *p1 == 0xF2) {
				lLen2 = MIN (3, INT_PTRtolong (pData + lLen - p1));
			}
			else if (0xC0 <= *p1 && *p1 <= 0xDF ||
				0xF1 == *p1 || *p1 == 0xF3) {
				lLen2 = MIN (2, INT_PTRtolong (pData + lLen - p1));
			}
			else if (0xF4 <= *p1 && *p1 <= 0xFF) {
				lLen2 = MIN (1, INT_PTRtolong (pData + lLen - p1));
			}
			lRet += MIDIOut_PutMIDIMessage (pMIDIOut, p1, lLen2);
			cRunningStatus = *p1;
			p1 += lLen2;
		}
	}
	return lRet;
}

/* このMIDI入力デバイスの名前を取得する(ANSI)(20120415追加) */
long __stdcall MIDIOut_GetThisDeviceNameA (MIDIOut* pMIDIOut, char* pszDeviceName, long lLen) {
	long lLen2;
	_ASSERT (pMIDIOut);
	_ASSERT (pMIDIOut->m_lMode == MIDIIO_MODEOUT);
	memset (pszDeviceName, 0, lLen);
	lLen2 = size_ttolong (strlen (pMIDIOut->m_pDeviceName));
	strncpy_s (pszDeviceName, lLen, pMIDIOut->m_pDeviceName, CLIP (0, lLen2, lLen - 1));
	return CLIP (0, lLen2, lLen - 1);
}

/* このMIDI入力デバイスの名前を取得する(UNICODE)(20120415追加) */
long __stdcall MIDIOut_GetThisDeviceNameW (MIDIOut* pMIDIOut, wchar_t* pszDeviceName, long lLen) {
	long lLen2;
	_ASSERT (pMIDIOut);
	_ASSERT (pMIDIOut->m_lMode == MIDIIO_MODEOUT);
	memset (pszDeviceName, 0, lLen * sizeof (wchar_t));
	lLen2 = size_ttolong (wcslen (pMIDIOut->m_pDeviceName));
	wcsncpy_s (pszDeviceName, lLen, pMIDIOut->m_pDeviceName, CLIP (0, lLen2, lLen - 1));
	return CLIP (0, lLen2, lLen - 1);
}

/******************************************************************************/
/*                                                                            */
/*  MIDIInクラス関数群                                                        */
/*                                                                            */
/******************************************************************************/


/* MIDI入力デバイスが開かれたときに呼び出される。(隠蔽) */
static void MIDIIn_OnMIMOpen 
(HMIDIIN hMIDIn, DWORD_PTR dwInstance, DWORD_PTR dwParam1, DWORD_PTR dwParam2) {
	;
}

/* MIDI入力デバイスが閉じられたときに呼び出される。(隠蔽) */
static void MIDIIn_OnMIMClose 
(HMIDIIN hMIDIIn, DWORD_PTR dwInstance, DWORD_PTR dwParam1, DWORD_PTR dwParam2) {
	;
}

/* MIDIデータ(SYSX除く)が入力されたときに呼び出される。(隠蔽) */
static void MIDIIn_OnMIMData 
(HMIDIIN hMIDIIn, DWORD_PTR dwInstance, DWORD_PTR dwParam1, DWORD_PTR dwParam2) {
	MIDI* pMIDIIn = (MIDI*)dwInstance;
	unsigned char cData[4];
	long lLen = 0;
	cData[0] = (unsigned char)(dwParam1 & 0x000000FF);
	cData[1] = (unsigned char)((dwParam1 & 0x0000FF00) >> 8);
	cData[2] = (unsigned char)((dwParam1 & 0x00FF0000) >> 16);
	cData[3] = (unsigned char)((dwParam1 & 0xFF000000) >> 24);

	if (0x80 <= cData[0] && cData[0] <= 0xBF ||
		0xE0 <= cData[0] && cData[0] <= 0xEF || cData[0] == 0xF2) { // 20090627修正
		lLen = 3;
	}
	else if (0xC0 <= cData[0] && cData[0] <= 0xDF ||
		cData[0] == 0xF1 || cData[0] == 0xF3) { // 20090626修正
		lLen = 2;
	}
	else if (cData[0] == 0xF6 || 0xF8 <= cData[0] && cData[0] <= 0xFF) { // 20090626修正
		lLen = 1;
	}
	/* SYSX受信はMIM_LONGDATAの仕事であるため、ここではエラーとみなす。 */
	else if (cData[0]== 0xF0) { // 20090626修正
		lLen = 0;
		_RPTF0 (_CRT_WARN, "警告：MIDIIn_OnMIMData内でSysxを検出しました。\r\n");
	}
	else { // 20090626追加
		lLen = 0;
		_RPTF0 (_CRT_WARN, "警告：MIDIIn_OnMIMData内で不明な種類のMIDIメッセージを検出しました。\r\n");
	}

	if (pMIDIIn->m_lWritePosition + lLen < pMIDIIn->m_lBufSize) {
		memcpy (pMIDIIn->m_pBuf + pMIDIIn->m_lWritePosition, &cData[0], lLen);
		pMIDIIn->m_lWritePosition += lLen;
	}
	else if (lLen < pMIDIIn->m_lBufSize) {
		memcpy (pMIDIIn->m_pBuf + pMIDIIn->m_lWritePosition, &cData[0], 
			pMIDIIn->m_lBufSize - pMIDIIn->m_lWritePosition);
		memcpy (pMIDIIn->m_pBuf, &cData[pMIDIIn->m_lBufSize - pMIDIIn->m_lWritePosition],
			lLen - (pMIDIIn->m_lBufSize - pMIDIIn->m_lWritePosition));
		pMIDIIn->m_lWritePosition = lLen - (pMIDIIn->m_lBufSize - pMIDIIn->m_lWritePosition);
	}
	else {
		_RPTF0 (_CRT_WARN, "警告：MIDIIn_OnMIMData内でバッファサイズより長いデータを検出しました。\r\n");
	}
	/* TODO 本来コールバック関数内でシステムコールは禁止であるが、 */
	/* 上記memcpyに限り、今のところ問題は発生していない。 */
}

/* MIDIデータ(SYSX)が入力されたときに呼び出される */
static void MIDIIn_OnMIMLongData 
(HMIDIIN hMIDIIn, DWORD_PTR dwInstance, DWORD_PTR dwParam1, DWORD_PTR dwParam2) {
	MIDI* pMIDIIn = (MIDI*)dwInstance;
	MIDIHDR* pMIDIHeader = (MIDIHDR*)dwParam1;
	unsigned char* pData = pMIDIHeader->lpData;
	long lLen = pMIDIHeader->dwBytesRecorded;
	unsigned long nRet;
	int i;
	/* 何番目のSYSXヘッダーが受信完了したか検出する */
	for (i = 0; i < MIDIIO_SYSXNUM; i++) {
		if (pMIDIIn->m_pSysxHeader[i] == pMIDIHeader) {
			break;
		}
	}
	if (i >= MIDIIO_SYSXNUM) {
		_RPTF0 (_CRT_WARN, "警告：MIDIIn_OnMIMLongData内で出所不明なSysxデータを検出しました。\r\n");
	}
	/* 入力データをバッファへコピー */
	MIDIIO_LockBuf (pMIDIIn);
#ifdef _DEBUG
	{
		char szMsg[16384];
		MIDIIO_Bin2Txt (pData, MIN (lLen, MIDIIO_MAXSYSXSIZE), szMsg, sizeof (szMsg));
		_RPTF1 (_CRT_WARN, "情報：MIDIIn_OnMIMLongData内でSysxを受信しました。{%s}\r\n", szMsg);
	}
#endif
	if (pMIDIIn->m_lWritePosition + lLen < pMIDIIn->m_lBufSize) {
		memcpy (pMIDIIn->m_pBuf + pMIDIIn->m_lWritePosition, pData, lLen);
		pMIDIIn->m_lWritePosition += lLen;
	}
	else if (lLen < pMIDIIn->m_lBufSize) {
		memcpy (pMIDIIn->m_pBuf + pMIDIIn->m_lWritePosition, pData, 
			pMIDIIn->m_lBufSize - pMIDIIn->m_lWritePosition);
		memcpy (pMIDIIn->m_pBuf, pData + pMIDIIn->m_lBufSize - pMIDIIn->m_lWritePosition,
			lLen - (pMIDIIn->m_lBufSize - pMIDIIn->m_lWritePosition));
		pMIDIIn->m_lWritePosition = lLen - (pMIDIIn->m_lBufSize - pMIDIIn->m_lWritePosition);
	}
	else {
		_RPTF0 (_CRT_WARN, "警告：MIDIIn_OnMIMLongData内でバッファサイズより長いSysxデータを検出しました。\r\n");
	}
	MIDIIO_UnLockBuf (pMIDIIn);

	/* midiInStart中はmidiInUnprepareHeaderできない */
	
	/* すでに入力停止している場合はreturn */
	if (pMIDIIn->m_bStarting == 0) {
		return;
	}
	/* 入力継続中の場合は使用済みSYSXヘッダーを再利用する */
	/* 使用済みSYSXヘッダーの再準備 */
	nRet = midiInPrepareHeader (hMIDIIn, pMIDIHeader, sizeof (MIDIHDR));
	if (nRet != MMSYSERR_NOERROR) {
		_RPTF1 (_CRT_WARN, "エラー：MIDIIn_OnMIMLongData内でmidiInPrepareHeaderが%dを返しました。\r\n", nRet);
		return;
	}
	/* 使用済みSYSXヘッダーの再登録 */
	midiInAddBuffer (hMIDIIn, pMIDIHeader, sizeof (MIDIHDR));
	if (nRet != MMSYSERR_NOERROR) {
		midiInUnprepareHeader (pMIDIIn->m_pDeviceHandle, pMIDIHeader, sizeof (MIDIHDR));
		_RPTF1 (_CRT_WARN, "エラー：MIDIIn_OnMIMLongData内でmidiInAddBufferが%dを返しました。\r\n", nRet);
	}
	/* TODO 本来コールバック関数内でシステムコールは禁止であるが、 */
	/* 上記memcpy,midiInPrepareHeader,midiInAddBufferに限り、今のところ問題は発生していない。 */

}

static void MIDIIn_OnMIMLongError 
(HMIDIIN hMIDIIn, DWORD_PTR dwInstance, DWORD_PTR dwParam1, DWORD_PTR dwParam2) {

}

static void MIDIIn_OnMIMError
(HMIDIIN hMIDIIn, DWORD_PTR dwInstance, DWORD_PTR dwParam1, DWORD_PTR dwParam2) {

}

/* MIDI入力デバイスからメッセージを受け取るコールバック関数。(隠蔽) */
static void CALLBACK MidiInProc (HMIDIIN hMIDIIn, UINT wMsg,         
  DWORD_PTR dwInstance, DWORD_PTR dwParam1, DWORD_PTR dwParam2) {
	switch (wMsg) {
	case MIM_OPEN:
		_RPTF3 (_CRT_WARN, "MIM_OPEN, 0x%p, 0x%p, 0x%p\r\n", 
			dwInstance, dwParam1, dwParam2);
		MIDIIn_OnMIMOpen (hMIDIIn, dwInstance, dwParam1, dwParam2);
		break;
	case MIM_CLOSE:
		_RPTF3 (_CRT_WARN, "MIM_CLOSE, 0x%p, 0x%p, 0x%p\r\n", 
			dwInstance, dwParam1, dwParam2);
		MIDIIn_OnMIMClose (hMIDIIn, dwInstance, dwParam1, dwParam2);
		break;
	case MIM_DATA:
		//_RPTF3 (_CRT_WARN, "MIM_DATA, 0x%p, 0x%p, 0x%p\r\n", 
		//	dwInstance, dwParam1, dwParam2);
		MIDIIn_OnMIMData (hMIDIIn, dwInstance, dwParam1, dwParam2);
		break;
	case MIM_LONGDATA:
		_RPTF3 (_CRT_WARN, "MIM_LONGDATA, 0x%p, 0x%p, 0x%p\r\n", 
			dwInstance, dwParam1, dwParam2);
		MIDIIn_OnMIMLongData (hMIDIIn, dwInstance, dwParam1, dwParam2);
		break;
	case MIM_LONGERROR:
		_RPTF3 (_CRT_WARN, "MIM_LONGERROR, 0x%p, 0x%p, 0x%p\r\n", 
			dwInstance, dwParam1, dwParam2);
		MIDIIn_OnMIMLongError (hMIDIIn, dwInstance, dwParam1, dwParam2);
		break;
	case MIM_ERROR:
		_RPTF3 (_CRT_WARN, "MIM_ERROR, 0x%p, 0x%p, 0x%p\r\n", 
			dwInstance, dwParam1, dwParam2);
		MIDIIn_OnMIMError (hMIDIIn, dwInstance, dwParam1, dwParam2);
		break;
	}
}

/* MIDI入力デバイスの数を調べる */
long __stdcall MIDIIn_GetDeviceNum () {
	return midiInGetNumDevs ();
}

/* MIDI入力デバイスの名前を調べる(ANSI) */
long __stdcall MIDIIn_GetDeviceNameA (long lID, char* pszDeviceName, long lLen) {
	int nRet;
	long lLen2;
	MIDIINCAPSA tagMIDIInCaps;
	memset (&tagMIDIInCaps, 0, sizeof (MIDIINCAPSA));
	nRet = midiInGetDevCapsA (lID, &tagMIDIInCaps, sizeof(MIDIINCAPSA));
	if (nRet != MMSYSERR_NOERROR) {
		return 0;
	}
	memset (pszDeviceName, 0, lLen);
	lLen2 = size_ttolong (strlen (tagMIDIInCaps.szPname));
	strncpy_s (pszDeviceName, lLen, tagMIDIInCaps.szPname, MIN (lLen - 1, lLen2));
	return MIN (lLen - 1, lLen2);
}

/* MIDI入力デバイスの名前を調べる(UNICODE) */
long __stdcall MIDIIn_GetDeviceNameW (long lID, wchar_t* pszDeviceName, long lLen) {
	int nRet;
	long lLen2;
	MIDIINCAPSW tagMIDIInCaps;
	memset (&tagMIDIInCaps, 0, sizeof (MIDIINCAPSW));
	nRet = midiInGetDevCapsW (lID, &tagMIDIInCaps, sizeof(MIDIINCAPSW));
	if (nRet != MMSYSERR_NOERROR) {
		return 0;
	}
	memset (pszDeviceName, 0, lLen * sizeof (wchar_t));
	lLen2 = size_ttolong (wcslen (tagMIDIInCaps.szPname));
	wcsncpy_s (pszDeviceName, lLen, tagMIDIInCaps.szPname, MIN (lLen - 1, lLen2));
	return MIN (lLen - 1, lLen2);
}


/* MIDI入力デバイスを開く(ANSI) */
MIDIIn* __stdcall MIDIIn_OpenA (const char* pszDeviceName) {
	MIDIINCAPSA tagMIDIInCaps;
	MIDI* pMIDIIn = NULL;
	MIDIHDR* pMIDIHdr = NULL;
	long lDeviceNameLen;
	int nNumDevices, i, nRet;
	if (pszDeviceName == NULL) {
		return NULL;
	}
	/* デバイス名が空文字列又は"None"又は"なし"の場合は何も開かない */
	if (*pszDeviceName == 0) {
		return NULL;
	}
	else if (strcmp (pszDeviceName, MIDIIO_NONE) == 0 ||
		strcmp (pszDeviceName, MIDIIO_NONEJ) == 0) {
		return NULL;
	}
	/* デバイス名を解析し何番目のMIDI出力デバイスか判定 */
	nNumDevices = midiInGetNumDevs ();
	for (i = 0; i < nNumDevices; i++) {
		memset (&tagMIDIInCaps, 0, sizeof (MIDIINCAPSA));
		nRet = midiInGetDevCapsA (i, &tagMIDIInCaps, sizeof(MIDIINCAPSA));
		if (strncmp (pszDeviceName, tagMIDIInCaps.szPname, 32) == 0)
			break;
	}
	if (i >= nNumDevices) {
		_RPTF1 (_CRT_WARN, "エラー：MIDIIn_OpenA内でMIDI入力デバイスが見つかりません。-%s\r\n", pszDeviceName);
		return NULL;
	}
	/* MIDIIn構造体の確保 */
	pMIDIIn = calloc (1, sizeof (MIDI));
	if (pMIDIIn == NULL) {
		_RPTF0 (_CRT_WARN, "エラー：MIDIIn_OpenA内でcallocがNULLを返しました。\r\n");
		return NULL;
	}
	lDeviceNameLen = size_ttolong (strlen (pszDeviceName) + 1);
	pMIDIIn->m_pDeviceName = calloc (lDeviceNameLen, sizeof (char));
	if (pMIDIIn->m_pDeviceName == NULL) {
		_RPTF0 (_CRT_WARN, "エラー：MIDIIn_OpenA内でcallocがNULLを返しました。\r\n");
		return NULL;
	}
	strncpy_s ((char*)(pMIDIIn->m_pDeviceName), lDeviceNameLen, pszDeviceName, lDeviceNameLen - 1);
	pMIDIIn->m_lMode = MIDIIO_MODEIN;

	MIDIIO_LockBuf (pMIDIIn);
	pMIDIIn->m_pBuf = malloc (MIDIIO_BUFSIZE);
	if (pMIDIIn->m_pBuf == NULL) {
		free (pMIDIIn);
		_RPTF0 (_CRT_WARN, "エラー：MIDIIn_OpenA内でmallocがNULLを返しました。\r\n");
		return NULL;
	}
	pMIDIIn->m_lBufSize = MIDIIO_BUFSIZE;
	MIDIIO_UnLockBuf (pMIDIIn);
	/* MIDI入力デバイスのオープン */
	nRet = midiInOpen ((HMIDIIN*)&(pMIDIIn->m_pDeviceHandle), i, 
		(DWORD_PTR)MidiInProc, (DWORD_PTR)(pMIDIIn), CALLBACK_FUNCTION);
	if (nRet != MMSYSERR_NOERROR) {
		free (pMIDIIn->m_pBuf);
		free (pMIDIIn);
		_RPTF1 (_CRT_WARN, "エラー：MIDIIn_OpenA内でmidiInOpenが%dを返しました。\r\n", nRet);
		return NULL;
	}
	/* SYSXヘッダーをの領域を確保する */
	for (i = 0; i < MIDIIO_SYSXNUM; i++) {
		pMIDIIn->m_pSysxHeader[i] = (MIDIHDR*)HeapAlloc (GetProcessHeap (),
			HEAP_ZERO_MEMORY, sizeof (MIDIHDR));
		if (pMIDIIn->m_pSysxHeader[i] == NULL) {
			free (pMIDIIn->m_pBuf);
			free (pMIDIIn);
			_RPTF1 (_CRT_WARN, "エラー：MIDIIn_OpenA内でHeapAllocが%dを返しました。\r\n", nRet);
			return NULL;
		}

		((MIDIHDR*)(pMIDIIn->m_pSysxHeader[i]))->lpData = 
			(char*)HeapAlloc (GetProcessHeap (), HEAP_NO_SERIALIZE, MIDIIO_MAXSYSXSIZE);
		if (((MIDIHDR*)(pMIDIIn->m_pSysxHeader[i]))->lpData == NULL) {
			free (pMIDIIn->m_pBuf);
			free (pMIDIIn);
			_RPTF1 (_CRT_WARN, "エラー：MIDIIn_OpenA内でHeapAllocが%dを返しました。\r\n", nRet);
			return NULL;
		}
		((MIDIHDR*)(pMIDIIn->m_pSysxHeader[i]))->dwBufferLength = MIDIIO_MAXSYSXSIZE;
	}
	/* SYSXヘッダーをすべて待機状態にする(スタート前処理) */
	for (i = 0; i < MIDIIO_SYSXNUM; i++) {
		nRet = midiInPrepareHeader (pMIDIIn->m_pDeviceHandle, pMIDIIn->m_pSysxHeader[i], sizeof (MIDIHDR));
		if (nRet != MMSYSERR_NOERROR) {
			HeapFree (GetProcessHeap (), 0, ((MIDIHDR*)(pMIDIIn->m_pSysxHeader[i]))->lpData);
			HeapFree (GetProcessHeap (), 0, (MIDIHDR*)(pMIDIIn->m_pSysxHeader[i]));		
			free (pMIDIIn->m_pBuf);
			free (pMIDIIn);
			_RPTF1 (_CRT_WARN, "エラー：MIDIIn_OpenA内でmidiInPrepareHeaderが%dを返しました。\r\n", nRet);
			return NULL;
		}

		nRet = midiInAddBuffer (pMIDIIn->m_pDeviceHandle, 
			(MIDIHDR*)(pMIDIIn->m_pSysxHeader[i]), sizeof (MIDIHDR));
		if (nRet != MMSYSERR_NOERROR) {
			midiInUnprepareHeader (pMIDIIn->m_pDeviceHandle, (MIDIHDR*)(pMIDIIn->m_pSysxHeader[i]), sizeof (MIDIHDR));
			HeapFree (GetProcessHeap (), 0, ((MIDIHDR*)(pMIDIIn->m_pSysxHeader[i]))->lpData);
			HeapFree (GetProcessHeap (), 0, (MIDIHDR*)(pMIDIIn->m_pSysxHeader[i]));		
			free (pMIDIIn->m_pBuf);
			free (pMIDIIn);
			_RPTF1 (_CRT_WARN, "エラー：MIDIIn_OpenA内でmidiInAddBufferが%dを返しました。\r\n", nRet);
			return NULL;
		}
	}
	/* 入力の開始(スタート) */
	nRet = midiInStart (pMIDIIn->m_pDeviceHandle);
	if (nRet != MMSYSERR_NOERROR) {
		for (i = 0; i < MIDIIO_SYSXNUM; i++) {
			midiInUnprepareHeader (pMIDIIn->m_pDeviceHandle, (MIDIHDR*)(pMIDIIn->m_pSysxHeader[i]), sizeof (MIDIHDR));
			HeapFree (GetProcessHeap (), 0, ((MIDIHDR*)(pMIDIIn->m_pSysxHeader[i]))->lpData);
			HeapFree (GetProcessHeap (), 0, (MIDIHDR*)(pMIDIIn->m_pSysxHeader[i]));	
		}
		free (pMIDIIn->m_pBuf);
		free (pMIDIIn);
		_RPTF1 (_CRT_WARN, "エラー：MIDIIn_OpenA内でmidiInStartが%dを返しました。\r\n", nRet);
		return NULL;
	}
	pMIDIIn->m_bStarting = 1;

	return pMIDIIn;
}


/* MIDI入力デバイスを開く(UNICODE) */
MIDIIn* __stdcall MIDIIn_OpenW (const wchar_t* pszDeviceName) {
	MIDIINCAPSW tagMIDIInCaps;
	MIDI* pMIDIIn = NULL;
	MIDIHDR* pMIDIHdr = NULL;
	long lDeviceNameLen;
	int nNumDevices, i, nRet;
	if (pszDeviceName == NULL) {
		return NULL;
	}
	/* デバイス名が空文字列又は"None"又は"なし"の場合は何も開かない */
	if (*pszDeviceName == 0) {
		return NULL;
	}
	else if (wcscmp (pszDeviceName, L"(None)") == 0 ||
		wcscmp (pszDeviceName, L"(なし)") == 0) {
		return NULL;
	}
	/* デバイス名を解析し何番目のMIDI出力デバイスか判定 */
	nNumDevices = midiInGetNumDevs ();
	for (i = 0; i < nNumDevices; i++) {
		memset (&tagMIDIInCaps, 0, sizeof (MIDIINCAPSW));
		nRet = midiInGetDevCapsW (i, &tagMIDIInCaps, sizeof(MIDIINCAPSW));
		if (wcsncmp (pszDeviceName, tagMIDIInCaps.szPname, 32) == 0)
			break;
	}
	if (i >= nNumDevices) {
		_RPTF1 (_CRT_WARN, "エラー：MIDIIn_OpenW内でMIDI入力デバイスが見つかりません。-%s\r\n", pszDeviceName);
		return NULL;
	}
	/* MIDIIn構造体の確保 */
	pMIDIIn = calloc (1, sizeof (MIDI));
	if (pMIDIIn == NULL) {
		_RPTF0 (_CRT_WARN, "エラー：MIDIIn_OpenW内でcallocがNULLを返しました。\r\n");
		return NULL;
	}
	lDeviceNameLen = size_ttolong (wcslen (pszDeviceName) + 1);
	pMIDIIn->m_pDeviceName = calloc (lDeviceNameLen, sizeof (wchar_t));
	if (pMIDIIn->m_pDeviceName == NULL) {
		_RPTF0 (_CRT_WARN, "エラー：MIDIIn_OpenW内でcallocがNULLを返しました。\r\n");
		return NULL;
	}
	wcsncpy_s ((wchar_t*)(pMIDIIn->m_pDeviceName), lDeviceNameLen, pszDeviceName, lDeviceNameLen - 1);
	pMIDIIn->m_lMode = MIDIIO_MODEIN;

	MIDIIO_LockBuf (pMIDIIn);
	pMIDIIn->m_pBuf = malloc (MIDIIO_BUFSIZE);
	if (pMIDIIn->m_pBuf == NULL) {
		free (pMIDIIn);
		_RPTF0 (_CRT_WARN, "エラー：MIDIIn_OpenW内でmallocがNULLを返しました。\r\n");
		return NULL;
	}
	pMIDIIn->m_lBufSize = MIDIIO_BUFSIZE;
	MIDIIO_UnLockBuf (pMIDIIn);
	/* MIDI入力デバイスのオープン */
	nRet = midiInOpen ((HMIDIIN*)&(pMIDIIn->m_pDeviceHandle), i, 
		(DWORD_PTR)MidiInProc, (DWORD_PTR)(pMIDIIn), CALLBACK_FUNCTION);
	if (nRet != MMSYSERR_NOERROR) {
		free (pMIDIIn->m_pBuf);
		free (pMIDIIn);
		_RPTF1 (_CRT_WARN, "エラー：MIDIIn_OpenW内でmidiInOpenが%dを返しました。\r\n", nRet);
		return NULL;
	}
	/* SYSXヘッダーをの領域を確保する */
	for (i = 0; i < MIDIIO_SYSXNUM; i++) {
		pMIDIIn->m_pSysxHeader[i] = (MIDIHDR*)HeapAlloc (GetProcessHeap (),
			HEAP_ZERO_MEMORY, sizeof (MIDIHDR));
		if (pMIDIIn->m_pSysxHeader[i] == NULL) {
			free (pMIDIIn->m_pBuf);
			free (pMIDIIn);
			_RPTF1 (_CRT_WARN, "エラー：MIDIIn_OpenW内でHeapAllocが%dを返しました。\r\n", nRet);
			return NULL;
		}

		((MIDIHDR*)(pMIDIIn->m_pSysxHeader[i]))->lpData = 
			(char*)HeapAlloc (GetProcessHeap (), HEAP_NO_SERIALIZE, MIDIIO_MAXSYSXSIZE);
		if (((MIDIHDR*)(pMIDIIn->m_pSysxHeader[i]))->lpData == NULL) {
			free (pMIDIIn->m_pBuf);
			free (pMIDIIn);
			_RPTF1 (_CRT_WARN, "エラー：MIDIIn_OpenW内でHeapAllocが%dを返しました。\r\n", nRet);
			return NULL;
		}
		((MIDIHDR*)(pMIDIIn->m_pSysxHeader[i]))->dwBufferLength = MIDIIO_MAXSYSXSIZE;
	}
	/* SYSXヘッダーをすべて待機状態にする(スタート前処理) */
	for (i = 0; i < MIDIIO_SYSXNUM; i++) {
		nRet = midiInPrepareHeader (pMIDIIn->m_pDeviceHandle, pMIDIIn->m_pSysxHeader[i], sizeof (MIDIHDR));
		if (nRet != MMSYSERR_NOERROR) {
			HeapFree (GetProcessHeap (), 0, ((MIDIHDR*)(pMIDIIn->m_pSysxHeader[i]))->lpData);
			HeapFree (GetProcessHeap (), 0, (MIDIHDR*)(pMIDIIn->m_pSysxHeader[i]));		
			free (pMIDIIn->m_pBuf);
			free (pMIDIIn);
			_RPTF1 (_CRT_WARN, "エラー：MIDIIn_OpenW内でmidiInPrepareHeaderが%dを返しました。\r\n", nRet);
			return NULL;
		}

		nRet = midiInAddBuffer (pMIDIIn->m_pDeviceHandle, 
			(MIDIHDR*)(pMIDIIn->m_pSysxHeader[i]), sizeof (MIDIHDR));
		if (nRet != MMSYSERR_NOERROR) {
			midiInUnprepareHeader (pMIDIIn->m_pDeviceHandle, (MIDIHDR*)(pMIDIIn->m_pSysxHeader[i]), sizeof (MIDIHDR));
			HeapFree (GetProcessHeap (), 0, ((MIDIHDR*)(pMIDIIn->m_pSysxHeader[i]))->lpData);
			HeapFree (GetProcessHeap (), 0, (MIDIHDR*)(pMIDIIn->m_pSysxHeader[i]));		
			free (pMIDIIn->m_pBuf);
			free (pMIDIIn);
			_RPTF1 (_CRT_WARN, "エラー：MIDIIn_OpenW内でmidiInAddBufferが%dを返しました。\r\n", nRet);
			return NULL;
		}
	}
	/* 入力の開始(スタート) */
	nRet = midiInStart (pMIDIIn->m_pDeviceHandle);
	if (nRet != MMSYSERR_NOERROR) {
		for (i = 0; i < MIDIIO_SYSXNUM; i++) {
			midiInUnprepareHeader (pMIDIIn->m_pDeviceHandle, (MIDIHDR*)(pMIDIIn->m_pSysxHeader[i]), sizeof (MIDIHDR));
			HeapFree (GetProcessHeap (), 0, ((MIDIHDR*)(pMIDIIn->m_pSysxHeader[i]))->lpData);
			HeapFree (GetProcessHeap (), 0, (MIDIHDR*)(pMIDIIn->m_pSysxHeader[i]));	
		}
		free (pMIDIIn->m_pBuf);
		free (pMIDIIn);
		_RPTF1 (_CRT_WARN, "エラー：MIDIIn_OpenW内でmidiInStartが%dを返しました。\r\n", nRet);
		return NULL;
	}
	pMIDIIn->m_bStarting = 1;

	return pMIDIIn;
}


/* MIDI入力デバイスを閉じる。 */
long __stdcall MIDIIn_Close (MIDIIn* pMIDIIn) {
	int nRet = 0;
	int i, t;
	if (pMIDIIn == NULL) {
		return 1;
	}
	_ASSERT (pMIDIIn->m_lMode == MIDIIO_MODEIN);
	/* 入力の停止 */
	nRet = midiInStop  (pMIDIIn->m_pDeviceHandle);
	if (nRet != MMSYSERR_NOERROR) {
		_RPTF1 (_CRT_WARN, "エラー：MIDIIn_Close内でmidiInStopが%dを返しました。\r\n", nRet);
		return 0;
	}
	pMIDIIn->m_bStarting = 0;
	/* MIDI入力のリセット(ここで未入力のSYSXバッファはコールバックに返されMHDR_DONEフラグがセットされる */
	nRet = midiInReset (pMIDIIn->m_pDeviceHandle);
	if (nRet != MMSYSERR_NOERROR) {
		_RPTF1 (_CRT_WARN, "エラー：MIDIIn_Close内でmidiInResetが%dを返しました。\r\n", nRet);
		return 0;
	}
	/* すべてのSYSXヘッダーに対してMIM_LONGDATAを待つ(100回試行) */
	for (t = 0; t < 100; t++) {
		for (i = 0; i < MIDIIO_SYSXNUM; i++) {
			if ((((MIDIHDR*)(pMIDIIn->m_pSysxHeader[i]))->dwFlags & MHDR_DONE) == 0) {
				break;
			}
		}
		if (i >= MIDIIO_SYSXNUM) {
			break;
		}
		Sleep (10);
	}
	if (t == 100) {
		_RPTF0 (_CRT_WARN, "警告：MIDIIn_Close内でMIM_LONGDATAがコールバックされていないバッファがあります。\r\n");
	}
	/* SYSXヘッダーの不準備(ストップ後処理) */
	for (i = 0; i < MIDIIO_SYSXNUM; i++) {
		nRet = midiInUnprepareHeader (pMIDIIn->m_pDeviceHandle, pMIDIIn->m_pSysxHeader[i], sizeof(MIDIHDR));
		if (nRet != MMSYSERR_NOERROR) {
			_RPTF1 (_CRT_WARN, "エラー：MIDIIn_Close内でmidiInUnprepareHeaderが%dを返しました。\r\n", nRet);
			return 0;
		}
	}
	/* SYSXヘッダーの解放(クローズ前処理) */
	for (i = 0; i < MIDIIO_SYSXNUM; i++) {
		HeapFree (GetProcessHeap(), 0, ((MIDIHDR*)(pMIDIIn->m_pSysxHeader[i]))->lpData);
		((MIDIHDR*)(pMIDIIn->m_pSysxHeader[i]))->lpData = NULL;
		HeapFree (GetProcessHeap(), 0, (MIDIHDR*)(pMIDIIn->m_pSysxHeader[i]));
		pMIDIIn->m_pSysxHeader[i] = NULL;
	}
	/* MIDI入力デバイスを閉じる */
	nRet = midiInClose (pMIDIIn->m_pDeviceHandle);
	if (nRet != MMSYSERR_NOERROR) {
		_RPTF1 (_CRT_WARN, "エラー：MIDIIn_Close内でmidiInCloseが%dを返しました。\r\n", nRet);
		return 0;
	}
	/* MIDI入力構造体の解放 */
	MIDIIO_LockBuf (pMIDIIn);
	free (pMIDIIn->m_pBuf);
	pMIDIIn->m_pBuf = NULL;
	pMIDIIn->m_lBufSize = 0;
	free (pMIDIIn->m_pDeviceName);
	pMIDIIn->m_pDeviceName = NULL;
	MIDIIO_UnLockBuf (pMIDIIn);
	free (pMIDIIn);
	return 1;
}

/* MIDI入力デバイスを再び開く(ANSI)。 */
MIDIIn* __stdcall MIDIIn_ReopenA (MIDIIn* pMIDIIn, const char* pszDeviceName) {
	int nRet = 0;
	if (pMIDIIn) {
		_ASSERT (pMIDIIn->m_lMode == MIDIIO_MODEIN);
		nRet = MIDIIn_Close (pMIDIIn);
		if (nRet == 0) {
			_RPTF1 (_CRT_WARN, "エラー：MIDIIn_ReopenA内でMIDIIn_Closeが%dを返しました。\r\n", nRet);
			return 0;
		}
		pMIDIIn = NULL;
	}
	return MIDIIn_OpenA (pszDeviceName);
}

/* MIDI入力デバイスを再び開く(UNICODE)。 */
MIDIIn* __stdcall MIDIIn_ReopenW (MIDIIn* pMIDIIn, const wchar_t* pszDeviceName) {
	int nRet = 0;
	if (pMIDIIn) {
		_ASSERT (pMIDIIn->m_lMode == MIDIIO_MODEIN);
		nRet = MIDIIn_Close (pMIDIIn);
		if (nRet == 0) {
			_RPTF1 (_CRT_WARN, "エラー：MIDIIn_ReopenW内でMIDIIn_Closeが%dを返しました。\r\n", nRet);
			return 0;
		}
		pMIDIIn = NULL;
	}
	return MIDIIn_OpenW (pszDeviceName);
}


/* MIDI入力デバイスをリセットする。 */
long __stdcall MIDIIn_Reset (MIDIIn* pMIDIIn) {
	int nRet = 0;
	_ASSERT (pMIDIIn);
	_ASSERT (pMIDIIn->m_lMode == MIDIIO_MODEIN);
	nRet = midiInReset (pMIDIIn->m_pDeviceHandle);
	if (nRet == MMSYSERR_NOERROR) {
		_RPTF1 (_CRT_WARN, "エラー：MIDIIn_Reset内でmidiInResetが%dを返しました。\r\n", nRet);
		return 0;
	}
	return 1;
}

/* MIDI入力デバイスからMIDIメッセージを1つ入力する */
long __stdcall MIDIIn_GetMIDIMessage (MIDIIn* pMIDIIn, unsigned char* pMessage, long lLen) {
	unsigned char cType;
	long lMessageLen, lCopyLen;
	unsigned char* p;
	_ASSERT (pMIDIIn);
	_ASSERT (pMIDIIn->m_lMode == MIDIIO_MODEIN);
	_ASSERT (pMessage);
	/* 取り込むデータがバッファ上にある */
	if (pMIDIIn->m_lReadPosition != pMIDIIn->m_lWritePosition) {
		cType = *(pMIDIIn->m_pBuf + pMIDIIn->m_lReadPosition);
		/* ノート・チャンネルアフター・コントロール・ピッチベンド */	
		if (0x80 <= cType && cType <= 0xBF ||
			0xE0 <= cType && cType <= 0xEF) {
			pMIDIIn->m_byRunningStatus = cType; // 20090627追加
			lMessageLen = 3;
		}
		/* プログラム・キーアフタータッチ */
		else if (0xC0 <= cType && cType <= 0xDF) {
			pMIDIIn->m_byRunningStatus = cType; // 20090627追加
			lMessageLen = 2;
		}
		/* システムリアルタイムメッセージ */
		else if (0xF8 <= cType && cType <= 0xFF) {
			lMessageLen = 1;
		}
		/* システムエクスクルーシヴメッセージ */
		else if (cType == 0xF0) { // 20090627修正
			lMessageLen = 0;
			p = pMIDIIn->m_pBuf + pMIDIIn->m_lReadPosition;
			do {
				p++;
				lMessageLen++;
				if (p >= pMIDIIn->m_pBuf + pMIDIIn->m_lBufSize) {
					p = pMIDIIn->m_pBuf;
				}
			} while (*p <= 0x7F);
			lMessageLen++;
		}
		/* MIDIタイムコードクォーターフレーム・ソングセレクト */
		else if (cType == 0xF1 || cType == 0xF3) { // 20090627追加
			lMessageLen = 2;
		}
		/* ソングポジションセレクタ */
		else if (cType == 0xF2) { // 20090627追加
			lMessageLen = 3;
		}
		/* チューンリクエスト */
		else if (cType == 0xF6) { // 20090627追加
			lMessageLen = 1;
		}
		/* 定義されていないシステムコモンメッセージ */
		else if ((cType & 0xF0) == 0xF0) { // 20090627追加
			_RPTF1 (_CRT_WARN, "エラー：MIDIIn_GetMIDIMessage内で定義されていない"
				"MIDIメッセージを検出しました(%d)。\r\n", cType);
			lMessageLen = 1;
		}
		/* ランニングステータスが省略されている場合(0x00<=cType<=0x7F) */
		else {
			if ((pMIDIIn->m_byRunningStatus & 0xF0) == 0xC0 || // 20090627追加
				(pMIDIIn->m_byRunningStatus & 0xF0) == 0xD0) {
				lMessageLen = 1;
			}
			else {
				lMessageLen = 2;
			}
		}
		lCopyLen = MIN (lMessageLen, lLen);
		MIDIIO_LockBuf (pMIDIIn);
		if (pMIDIIn->m_lReadPosition + lMessageLen < pMIDIIn->m_lBufSize) {
			memcpy (pMessage, pMIDIIn->m_pBuf + pMIDIIn->m_lReadPosition, lCopyLen);
			pMIDIIn->m_lReadPosition += lMessageLen;
		}
		else {
			memcpy (pMessage, pMIDIIn->m_pBuf + pMIDIIn->m_lReadPosition, 
				pMIDIIn->m_lBufSize - pMIDIIn->m_lReadPosition); 
			memcpy (pMessage + (pMIDIIn->m_lBufSize - pMIDIIn->m_lReadPosition),
				pMIDIIn->m_pBuf,
				lCopyLen - (pMIDIIn->m_lBufSize - pMIDIIn->m_lReadPosition));
			pMIDIIn->m_lReadPosition = lMessageLen - 
				(pMIDIIn->m_lBufSize - pMIDIIn->m_lReadPosition);
		}
		MIDIIO_UnLockBuf (pMIDIIn);
		return lCopyLen;
	}
	/* 取り込むデータがバッファ上にない */
	else {
		return 0;
	}
}

/* MIDI入力デバイスから1バイト入力する */
long __stdcall MIDIIn_GetByte (MIDIIn* pMIDIIn, unsigned char* p) {
	long lRet;
	_ASSERT (pMIDIIn);
	_ASSERT (pMIDIIn->m_lMode == MIDIIO_MODEIN);
	_ASSERT (p);
	MIDIIO_LockBuf (pMIDIIn);
	if (pMIDIIn->m_lReadPosition != pMIDIIn->m_lWritePosition) {
		*p = *(pMIDIIn->m_pBuf + pMIDIIn->m_lReadPosition);
		pMIDIIn->m_lReadPosition++;
		if (pMIDIIn->m_lReadPosition >= pMIDIIn->m_lBufSize) {
			pMIDIIn->m_lReadPosition = 0;
		}
		lRet = 1;
	}
	else {
		lRet = 0;
	}
	MIDIIO_UnLockBuf (pMIDIIn);
	return lRet;
}

/* MIDI入力デバイスから任意長のデータを入力する */
long __stdcall MIDIIn_GetBytes (MIDIIn* pMIDIIn, unsigned char* pData, long lLen) {
	unsigned char cType;
	long lLen2, lCopyLen;
	_ASSERT (pMIDIIn);
	_ASSERT (pMIDIIn->m_lMode == MIDIIO_MODEIN);
	_ASSERT (pData);
	/* 取り込むデータがバッファ上にある */
	if (pMIDIIn->m_lReadPosition != pMIDIIn->m_lWritePosition) {
		cType = *(pMIDIIn->m_pBuf + pMIDIIn->m_lReadPosition);
		lLen2 = pMIDIIn->m_lWritePosition - pMIDIIn->m_lReadPosition;
		if (lLen2 < 0) {
			lLen2 += MIDIIO_BUFSIZE;
		}
		lCopyLen = MIN (lLen, lLen2);
		MIDIIO_LockBuf (pMIDIIn);
		if (pMIDIIn->m_lReadPosition + lCopyLen < pMIDIIn->m_lBufSize) {
			memcpy (pData, pMIDIIn->m_pBuf + pMIDIIn->m_lReadPosition, lCopyLen);
			pMIDIIn->m_lReadPosition += lCopyLen;
		}
		else {
			memcpy (pData, pMIDIIn->m_pBuf + pMIDIIn->m_lReadPosition, 
				pMIDIIn->m_lBufSize - pMIDIIn->m_lReadPosition); 
			memcpy (pData + (pMIDIIn->m_lBufSize - pMIDIIn->m_lReadPosition),
				pMIDIIn->m_pBuf,
				lCopyLen - (pMIDIIn->m_lBufSize - pMIDIIn->m_lReadPosition));
			pMIDIIn->m_lReadPosition = lCopyLen - 
				(pMIDIIn->m_lBufSize - pMIDIIn->m_lReadPosition);
		}
		MIDIIO_UnLockBuf (pMIDIIn);
		return lCopyLen;
	}
	/* 取り込むデータがバッファ上にない */
	else {
		return 0;
	}
}

/* このMIDI入力デバイスの名前を取得する(ANSI)(20120415追加) */
long __stdcall MIDIIn_GetThisDeviceNameA (MIDIIn* pMIDIIn, char* pszDeviceName, long lLen) {
	long lLen2;
	_ASSERT (pMIDIIn);
	_ASSERT (pMIDIIn->m_lMode == MIDIIO_MODEIN);
	memset (pszDeviceName, 0, lLen);
	lLen2 = size_ttolong (strlen (pMIDIIn->m_pDeviceName));
	strncpy_s (pszDeviceName, lLen, pMIDIIn->m_pDeviceName, CLIP (0, lLen2, lLen - 1));
	return CLIP (0, lLen2, lLen - 1);
}

/* このMIDI入力デバイスの名前を取得する(UNICODE)(20120415追加) */
long __stdcall MIDIIn_GetThisDeviceNameW (MIDIIn* pMIDIIn, wchar_t* pszDeviceName, long lLen) {
	long lLen2; 
	_ASSERT (pMIDIIn);
	_ASSERT (pMIDIIn->m_lMode == MIDIIO_MODEIN);
	memset (pszDeviceName, 0, lLen * sizeof (wchar_t));
	lLen2 = size_ttolong (wcslen (pMIDIIn->m_pDeviceName));
	wcsncpy_s (pszDeviceName, lLen, pMIDIIn->m_pDeviceName, CLIP (0, lLen2, lLen - 1));
	return CLIP (0, lLen2, lLen - 1);
}
