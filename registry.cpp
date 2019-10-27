﻿/*=============================================================================
*								レジストリ関係
*
*
===============================================================================
/ Copyright (C) 1997-2007 Sota. All rights reserved.
/
/ Redistribution and use in source and binary forms, with or without 
/ modification, are permitted provided that the following conditions 
/ are met:
/
/  1. Redistributions of source code must retain the above copyright 
/     notice, this list of conditions and the following disclaimer.
/  2. Redistributions in binary form must reproduce the above copyright 
/     notice, this list of conditions and the following disclaimer in the 
/     documentation and/or other materials provided with the distribution.
/
/ THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR 
/ IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES 
/ OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED. 
/ IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT, 
/ INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, 
/ BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF 
/ USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON 
/ ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT 
/ (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF 
/ THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
/============================================================================*/

#include "common.h"
const int AES_BLOCK_SIZE = 16;
static int EncryptSettings = NO;
static void MaskSettingsData(std::string_view salt, void* Data, DWORD Size, bool EscapeZero);
static void UnmaskSettingsData(std::string_view salt, void* Data, DWORD Size, bool EscapeZero);

static inline auto a2w(std::string_view text) {
	return convert<wchar_t>([](auto src, auto srclen, auto dst, auto dstlen) { return MultiByteToWideChar(CP_ACP, MB_ERR_INVALID_CHARS, src, srclen, dst, dstlen); }, text);
}

class Config {
protected:
	Config(std::string const& keyName) : KeyName{ keyName } {}
	virtual std::optional<int> ReadInt(std::string_view name) const = 0;
	virtual std::optional<std::string> ReadValue(std::string_view name) const = 0;
	virtual void Write(const char* name, int value) = 0;
	virtual void Write(const char* name, std::string_view value, DWORD type) = 0;
public:
	const std::string KeyName;
	virtual ~Config() = default;
	int ReadIntValueFromReg(char* Name, int* Value) {
		if (auto const read = ReadInt(Name)) {
			*Value = *read;
			if (EncryptSettings == YES)
				UnmaskSettingsData(KeyName + '\\' + Name, Value, sizeof(int), false);
			return FFFTP_SUCCESS;
		}
		return FFFTP_FAIL;
	}
	void WriteIntValueToReg(char* Name, int Value) {
		if (EncryptSettings == YES)
			MaskSettingsData(KeyName + '\\' + Name, &Value, sizeof(int), false);
		Write(Name, Value);
		if (EncryptSettings == YES)
			UnmaskSettingsData(KeyName + '\\' + Name, &Value, sizeof(int), false);
	}
	int ReadStringFromReg(char* Name, _Out_writes_z_(Size) char* Str, DWORD Size) {
		if (auto const read = ReadValue(Name)) {
			strncpy_s(Str, Size, read->c_str(), _TRUNCATE);
			if (EncryptSettings == YES)
				UnmaskSettingsData(KeyName + '\\' + Name, Str, (DWORD)strlen(Str) + 1, true);
			return FFFTP_SUCCESS;
		}
		return FFFTP_FAIL;
	}
	void WriteStringToReg(char* Name, char* Str) {
		if (EncryptSettings == YES)
			MaskSettingsData(KeyName + '\\' + Name, Str, (DWORD)strlen(Str) + 1, true);
		Write(Name, Str, REG_SZ);
		if (EncryptSettings == YES)
			UnmaskSettingsData(KeyName + '\\' + Name, Str, (DWORD)strlen(Str) + 1, true);
	}
	int ReadMultiStringFromReg(char* Name, char* Str, DWORD Size) {
		if (auto const read = ReadValue(Name)) {
			auto const len = std::min(read->size(), (size_t)Size - 1);
			std::copy_n(read->data(), len, Str);
			Str[len] = '\0';
			if (EncryptSettings == YES)
				UnmaskSettingsData(KeyName + '\\' + Name, Str, StrMultiLen(Str) + 1, true);
			return FFFTP_SUCCESS;
		}
		return FFFTP_FAIL;
	}
	void WriteMultiStringToReg(char* Name, char* Str) {
		if (EncryptSettings == YES)
			MaskSettingsData(KeyName +'\\' + Name, Str, StrMultiLen(Str) + 1, true);
		Write(Name, { Str, (size_t)StrMultiLen(Str) }, REG_MULTI_SZ);
		if (EncryptSettings == YES)
			UnmaskSettingsData(KeyName + '\\' + Name, Str, StrMultiLen(Str) + 1, true);
	}
	int ReadBinaryFromReg(char* Name, void* Bin, DWORD Size) {
		if (auto const read = ReadValue(Name)) {
			std::copy_n(read->data(), std::min(read->size(), (size_t)Size), reinterpret_cast<char*>(Bin));
			if (EncryptSettings == YES)
				UnmaskSettingsData(KeyName + '\\' + Name, Bin, Size, false);
			return FFFTP_SUCCESS;
		}
		return FFFTP_FAIL;
	}
	void WriteBinaryToReg(char* Name, void* Bin, int Len) {
		if (EncryptSettings == YES)
			MaskSettingsData(KeyName + '\\' + Name, Bin, Len, false);
		Write(Name, { reinterpret_cast<const char*>(Bin), (size_t)Len }, REG_BINARY);
		if (EncryptSettings == YES)
			UnmaskSettingsData(KeyName + '\\' + Name, Bin, Len, false);
	}
};

static void SaveStr(Config* Handle, char *Key, char *Str, char *DefaultStr);
static void SaveIntNum(Config* Handle, char *Key, int Num, int DefaultNum);
static std::wstring MakeFontData(HFONT hfont, LOGFONTW const& logFont);
static std::optional<LOGFONTW> RestoreFontData(const wchar_t* str);

static void EncodePassword(std::string_view const& Str, char *Buf);

static void DecodePassword(char *Str, char *Buf);
static void DecodePasswordOriginal(char *Str, char *Buf);
static void DecodePassword2(char *Str, char *Buf, const char *Key);
static void DecodePassword3(char *Str, char *Buf);
static bool CreateAesKey(unsigned char *AesKey);

static void SetRegType(int Type);
static int OpenReg(char *Name, Config** Handle);
static int CreateReg(char *Name, Config** Handle);
static void CloseReg(Config* Handle);
static int OpenSubKey(Config* Parent, char *Name, Config** Handle);
static int CreateSubKey(Config* Parent, char *Name, Config** Handle);
static int CloseSubKey(Config* Handle);
static int DeleteSubKey(Config* Handle, char *Name);
static int DeleteValue(Config* Handle, char *Name);

// 全設定暗号化対応
//int CheckPasswordValidity( char* Password, int length, const char* HashStr );
//void CreatePasswordHash( char* Password, int length, char* HashStr );
int CheckPasswordValidity( char* Password, int length, const char* HashStr, int StretchCount );
void CreatePasswordHash( char* Password, int length, char* HashStr, int StretchCount );
void SetHashSalt( DWORD salt );
// 全設定暗号化対応
void SetHashSalt1(void* Salt, int Length);

/* 2010.01.30 genta 追加 */
static char SecretKey[FMAX_PATH+1];
static int SecretKeyLength;
static int IsMasterPasswordError = PASSWORD_OK;

static int IsRndSourceInit = 0;
static ulong RndSource[9];

// UTF-8対応
static int IniKanjiCode = KANJI_NOCNV;
static int EncryptSettingsError = NO;

/*===== 外部参照 =====*/

/* 設定値 */
extern int WinPosX;
extern int WinPosY;
extern int WinWidth;
extern int WinHeight;
extern int LocalWidth;
extern int TaskHeight;
extern int LocalTabWidth[4];
extern int RemoteTabWidth[6];
extern char UserMailAdrs[USER_MAIL_LEN+1];
extern char ViewerName[VIEWERS][FMAX_PATH+1];
extern HFONT ListFont;
extern LOGFONTW ListLogFont;
extern int LocalFileSort;
extern int LocalDirSort;
extern int RemoteFileSort;
extern int RemoteDirSort;
extern int TransMode;
extern int ConnectOnStart;
extern int DebugConsole;
extern int SaveWinPos;
extern char AsciiExt[ASCII_EXT_LEN+1];
extern int RecvMode;
extern int SendMode;
extern int MoveMode;
extern int ListType;
extern char DefaultLocalPath[FMAX_PATH+1];
extern int SaveTimeStamp;
extern int FindMode;
extern int DotFile;
extern int DclickOpen;
extern SOUNDFILE Sound[SOUND_TYPES];
extern int FnameCnv;
extern int ConnectAndSet;
extern int TimeOut;
extern int RmEOF;
extern int RegType;
extern char FwallHost[HOST_ADRS_LEN+1];
extern char FwallUser[USER_NAME_LEN+1];
extern char FwallPass[PASSWORD_LEN+1];
extern int FwallPort;
extern int FwallType;
extern int FwallDefault;
extern int FwallSecurity;
extern int FwallResolve;
extern int FwallLower;
extern int FwallDelimiter;
extern int PasvDefault;
extern char MirrorNoTrn[MIRROR_LEN+1];
extern char MirrorNoDel[MIRROR_LEN+1];
extern int MirrorFnameCnv;
//extern int MirrorFolderCnv;
extern int RasClose;
extern int RasCloseNotify;
extern int FileHist;
extern char DefAttrList[DEFATTRLIST_LEN+1];
extern SIZE HostDlgSize;
extern SIZE BmarkDlgSize;
extern SIZE MirrorDlgSize;
extern int Sizing;
extern int SortSave;
extern int QuickAnonymous;
extern int PassToHist;
extern int VaxSemicolon;
extern int SendQuit;
extern int NoRasControl;
extern int SuppressSave;

extern int UpExistMode;
extern int ExistMode;
extern int DispDrives;
extern int MirUpDelNotify;
extern int MirDownDelNotify;

extern int FolderAttr;
extern int FolderAttrNum;

// ファイルアイコン表示対応
extern int DispFileIcon;
// タイムスタンプのバグ修正
extern int DispTimeSeconds;
// ファイルの属性を数字で表示
extern int DispPermissionsNumber;
// ディレクトリ自動作成
extern int MakeAllDir;
// UTF-8対応
extern int LocalKanjiCode;
// UPnP対応
extern int UPnPEnabled;
// 全設定暗号化対応
extern int EncryptAllSettings;
// ローカル側自動更新
extern int AutoRefreshFileList;
// 古い処理内容を消去
extern int RemoveOldLog;
// バージョン確認
extern int ReadOnlySettings;
// ファイル一覧バグ修正
extern int AbortOnListError;
// ミラーリング設定追加
extern int MirrorNoTransferContents;
// FireWall設定追加
extern int FwallNoSaveUser;
// ゾーンID設定追加
extern int MarkAsInternet;


static void sha1(_In_reads_bytes_(datalen) const void* data, DWORD datalen, _Out_writes_bytes_(20) BYTE* buffer) {
	HCRYPTHASH hash;
	auto result = CryptCreateHash(HCryptProv, CALG_SHA1, 0, 0, &hash);
	assert(result);
	result = CryptHashData(hash, reinterpret_cast<const BYTE*>(data), datalen, 0);
	assert(result);
	DWORD hashlen = 20;
	result = CryptGetHashParam(hash, HP_HASHVAL, buffer, &hashlen, 0);
	assert(result && hashlen == 20);
	result = CryptDestroyHash(hash);
	assert(result);
}

static void sha_memory(const char* mem, DWORD length, uint32_t* buffer) {
	// ビット反転の必要がある
	sha1(mem, length, reinterpret_cast<BYTE*>(buffer));
	for (int i = 0; i < 5; i++)
		buffer[i] = _byteswap_ulong(buffer[i]);
}


/*----- マスタパスワードの設定 ----------------------------------------------
*
*	Parameter
*		const char* Password : マスターパスワード
*
*	Return Value
*		なし
*----------------------------------------------------------------------------*/
void SetMasterPassword( const char* Password )
{
	ZeroMemory( SecretKey, MAX_PASSWORD_LEN + 12 );
	if( Password != NULL ){
		strncpy_s(SecretKey, Password, MAX_PASSWORD_LEN);
	}
	else {
		strcpy( SecretKey, DEFAULT_PASSWORD );
	}
	SecretKeyLength = (int)strlen( SecretKey );
	
	/* 未検証なので，初期状態はOKにする (強制再設定→保存にを可能にする)*/
	IsMasterPasswordError = PASSWORD_OK;
}

// セキュリティ強化
void GetMasterPassword(char* Password)
{
	strcpy(Password, SecretKey);
}

/*----- マスタパスワードの状態取得 ----------------------------------------------
*
*	Parameter
*		なし
*
*	Return Value
*		PASSWORD_OK : OK
*		PASSWORD_UNMATCH : パスワード不一致
*		BAD_PASSWORD_HASH : パスワード確認失敗
*----------------------------------------------------------------------------*/
int GetMasterPasswordStatus(void)
{
	return IsMasterPasswordError;
}

/*----- レジストリ／INIファイルのマスターパスワードの検証を行う ------------
*
*	Parameter
*		なし
*
*	Return Value
*		
*----------------------------------------------------------------------------*/

int ValidateMasterPassword(void)
{
	Config* hKey3;
	int i;

	SetRegType(REGTYPE_INI);
	if((i = OpenReg("FFFTP", &hKey3)) != FFFTP_SUCCESS)
	{
		if(AskForceIni() == NO)
		{
			SetRegType(REGTYPE_REG);
			i = OpenReg("FFFTP", &hKey3);
		}
	}
	if(i == FFFTP_SUCCESS){
		char checkbuf[48];
		int salt = 0;
		// 全設定暗号化対応
		int stretch = 0;
		unsigned char salt1[16];

		if(hKey3->ReadStringFromReg("CredentialCheck1", checkbuf, sizeof(checkbuf)) == FFFTP_SUCCESS)
		{
			if(hKey3->ReadBinaryFromReg("CredentialSalt1", &salt1, sizeof(salt1)) == FFFTP_SUCCESS)
				SetHashSalt1(&salt1, 16);
			else
				SetHashSalt1(NULL, 0);
			hKey3->ReadIntValueFromReg("CredentialStretch", &stretch);
			switch(CheckPasswordValidity(SecretKey, SecretKeyLength, checkbuf, stretch))
			{
			case 0:
				IsMasterPasswordError = PASSWORD_UNMATCH;
				break;
			case 1:
				IsMasterPasswordError = PASSWORD_OK;
				break;
			default:
				IsMasterPasswordError = BAD_PASSWORD_HASH;
				break;
			}
		}
		else if(hKey3->ReadStringFromReg("CredentialCheck", checkbuf, sizeof(checkbuf)) == FFFTP_SUCCESS)
		{
			if(hKey3->ReadIntValueFromReg("CredentialSalt", &salt) == FFFTP_SUCCESS)
				SetHashSalt(salt);
			else
				SetHashSalt1(NULL, 0);
			switch(CheckPasswordValidity(SecretKey, SecretKeyLength, checkbuf, 0))
			{
			case 0:
				IsMasterPasswordError = PASSWORD_UNMATCH;
				break;
			case 1:
				IsMasterPasswordError = PASSWORD_OK;
				break;
			default:
				IsMasterPasswordError = BAD_PASSWORD_HASH;
				break;
			}
		}
		CloseReg(hKey3);
		return YES;
	}
	return NO;
}

/*----- レジストリ／INIファイルに設定値を保存 ---------------------------------
*
*	Parameter
*		なし
*
*	Return Value
*		なし
*----------------------------------------------------------------------------*/

void SaveRegistry(void)
{
	Config* hKey3;
	Config* hKey4;
	Config* hKey5;
	// 暗号化通信対応
//	char Str[FMAX_PATH+1];
	char Str[PRIVATE_KEY_LEN*4+1];
	char Buf[FMAX_PATH+1];
	int i;
	int n;
	HOSTDATA DefaultHost;
	HOSTDATA Host;
	HISTORYDATA Hist;
	HISTORYDATA DefaultHist;
	
	if( GetMasterPasswordStatus() == PASSWORD_UNMATCH ){
		/* 2010.01.30 genta: マスターパスワードが不一致の場合は不用意に上書きしない */
		return;
	}

	// 全設定暗号化対応
	if(EncryptSettingsError == YES)
		return;

	// バージョン確認
	if(ReadOnlySettings == YES)
		return;

	SetRegType(RegType);
	if(CreateReg("FFFTP", &hKey3) == FFFTP_SUCCESS)
	{
		char buf[48];
		int salt = GetTickCount();
		// 全設定暗号化対応
		unsigned char salt1[16];
		FILETIME ft[4];
	
		hKey3->WriteIntValueToReg("Version", VER_NUM);
		if(EncryptAllSettings == YES)
		{
			GetProcessTimes(GetCurrentProcess(), &ft[0], &ft[1], &ft[2], &ft[3]);
			memcpy(&salt1[0], &salt, 4);
			memcpy(&salt1[4], &ft[0].dwLowDateTime, 4);
			memcpy(&salt1[8], &ft[2].dwLowDateTime, 4);
			memcpy(&salt1[12], &ft[3].dwLowDateTime, 4);
			SetHashSalt1(&salt1, 16);
			hKey3->WriteBinaryToReg("CredentialSalt1", &salt1, sizeof(salt1));
			hKey3->WriteIntValueToReg("CredentialStretch", 65535);
			CreatePasswordHash(SecretKey, SecretKeyLength, buf, 65535);
			hKey3->WriteStringToReg("CredentialCheck1", buf);
		}
		else
		{
			SetHashSalt( salt );
			hKey3->WriteIntValueToReg("CredentialSalt", salt);
			CreatePasswordHash(SecretKey, SecretKeyLength, buf, 0);
			hKey3->WriteStringToReg("CredentialCheck", buf);
		}

		// 全設定暗号化対応
		hKey3->WriteIntValueToReg("EncryptAll", EncryptAllSettings);
		sprintf(Buf, "%d", EncryptAllSettings);
		EncodePassword(Buf, Str);
		hKey3->WriteStringToReg("EncryptAllDetector", Str);
		EncryptSettings = EncryptAllSettings;

		// 全設定暗号化対応
//		if(CreateSubKey(hKey3, "Options", &hKey4) == FFFTP_SUCCESS)
		if(EncryptAllSettings == YES)
			strcpy(Str, "EncryptedOptions");
		else
			strcpy(Str, "Options");
		if(CreateSubKey(hKey3, Str, &hKey4) == FFFTP_SUCCESS)
		{
			hKey4->WriteIntValueToReg("NoSave", SuppressSave);

			if(SuppressSave != YES)
			{
				hKey4->WriteIntValueToReg("WinPosX", WinPosX);
				hKey4->WriteIntValueToReg("WinPosY", WinPosY);
				hKey4->WriteIntValueToReg("WinWidth", WinWidth);
				hKey4->WriteIntValueToReg("WinHeight", WinHeight);
				hKey4->WriteIntValueToReg("LocalWidth", LocalWidth);
				hKey4->WriteIntValueToReg("TaskHeight", TaskHeight);
				hKey4->WriteBinaryToReg("LocalColm", LocalTabWidth, sizeof(LocalTabWidth));
				hKey4->WriteBinaryToReg("RemoteColm", RemoteTabWidth, sizeof(RemoteTabWidth));
				hKey4->WriteIntValueToReg("SwCmd", Sizing);

				hKey4->WriteStringToReg("UserMail", UserMailAdrs);
				hKey4->WriteStringToReg("Viewer", ViewerName[0]);
				hKey4->WriteStringToReg("Viewer2", ViewerName[1]);
				hKey4->WriteStringToReg("Viewer3", ViewerName[2]);

				hKey4->WriteIntValueToReg("TrType", TransMode);
				hKey4->WriteIntValueToReg("Recv", RecvMode);
				hKey4->WriteIntValueToReg("Send", SendMode);
				hKey4->WriteIntValueToReg("Move", MoveMode);
				hKey4->WriteStringToReg("Path", DefaultLocalPath);
				hKey4->WriteIntValueToReg("Time", SaveTimeStamp);
				hKey4->WriteIntValueToReg("EOF", RmEOF);
				hKey4->WriteIntValueToReg("Scolon", VaxSemicolon);

				hKey4->WriteIntValueToReg("RecvEx", ExistMode);
				hKey4->WriteIntValueToReg("SendEx", UpExistMode);

				hKey4->WriteIntValueToReg("LFsort", LocalFileSort);
				hKey4->WriteIntValueToReg("LDsort", LocalDirSort);
				hKey4->WriteIntValueToReg("RFsort", RemoteFileSort);
				hKey4->WriteIntValueToReg("RDsort", RemoteDirSort);
				hKey4->WriteIntValueToReg("SortSave", SortSave);

				hKey4->WriteIntValueToReg("ListType", ListType);
				hKey4->WriteIntValueToReg("DotFile", DotFile);
				hKey4->WriteIntValueToReg("Dclick", DclickOpen);

				hKey4->WriteIntValueToReg("ConS", ConnectOnStart);
				hKey4->WriteIntValueToReg("OldDlg", ConnectAndSet);
				hKey4->WriteIntValueToReg("RasClose", RasClose);
				hKey4->WriteIntValueToReg("RasNotify", RasCloseNotify);
				hKey4->WriteIntValueToReg("Qanony", QuickAnonymous);
				hKey4->WriteIntValueToReg("PassHist", PassToHist);
				hKey4->WriteIntValueToReg("SendQuit", SendQuit);
				hKey4->WriteIntValueToReg("NoRas", NoRasControl);

				hKey4->WriteIntValueToReg("Debug", DebugConsole);
				hKey4->WriteIntValueToReg("WinPos", SaveWinPos);
				hKey4->WriteIntValueToReg("RegExp", FindMode);
				hKey4->WriteIntValueToReg("Reg", RegType);

				hKey4->WriteMultiStringToReg("AsciiFile", AsciiExt);
				hKey4->WriteIntValueToReg("LowUp", FnameCnv);
				hKey4->WriteIntValueToReg("Tout", TimeOut);

				hKey4->WriteMultiStringToReg("NoTrn", MirrorNoTrn);
				hKey4->WriteMultiStringToReg("NoDel", MirrorNoDel);
				hKey4->WriteIntValueToReg("MirFile", MirrorFnameCnv);
				hKey4->WriteIntValueToReg("MirUNot", MirUpDelNotify);
				hKey4->WriteIntValueToReg("MirDNot", MirDownDelNotify);

				strcpy(Str, u8(MakeFontData(ListFont, ListLogFont)).c_str());
				hKey4->WriteStringToReg("ListFont", Str);
				hKey4->WriteIntValueToReg("ListHide", DispIgnoreHide);
				hKey4->WriteIntValueToReg("ListDrv", DispDrives);

				hKey4->WriteStringToReg("FwallHost", FwallHost);
				if(FwallNoSaveUser == YES)
				{
					hKey4->WriteStringToReg("FwallUser", "");
					EncodePassword("", Str);
				}
				else
				{
					hKey4->WriteStringToReg("FwallUser", FwallUser);
					EncodePassword(FwallPass, Str);
				}
				hKey4->WriteStringToReg("FwallPass", Str);
				hKey4->WriteIntValueToReg("FwallPort", FwallPort);
				hKey4->WriteIntValueToReg("FwallType", FwallType);
				hKey4->WriteIntValueToReg("FwallDef", FwallDefault);
				hKey4->WriteIntValueToReg("FwallSec", FwallSecurity);
				hKey4->WriteIntValueToReg("PasvDef", PasvDefault);
				hKey4->WriteIntValueToReg("FwallRes", FwallResolve);
				hKey4->WriteIntValueToReg("FwallLow", FwallLower);
				hKey4->WriteIntValueToReg("FwallDel", FwallDelimiter);

				hKey4->WriteIntValueToReg("SndConSw", Sound[SND_CONNECT].On);
				hKey4->WriteIntValueToReg("SndTrnSw", Sound[SND_TRANS].On);
				hKey4->WriteIntValueToReg("SndErrSw", Sound[SND_ERROR].On);
				hKey4->WriteStringToReg("SndCon", Sound[SND_CONNECT].Fname);
				hKey4->WriteStringToReg("SndTrn", Sound[SND_TRANS].Fname);
				hKey4->WriteStringToReg("SndErr", Sound[SND_ERROR].Fname);

				hKey4->WriteMultiStringToReg("DefAttr", DefAttrList);

				hKey4->WriteBinaryToReg("Hdlg", &HostDlgSize, sizeof(SIZE));
				hKey4->WriteBinaryToReg("Bdlg", &BmarkDlgSize, sizeof(SIZE));
				hKey4->WriteBinaryToReg("Mdlg", &MirrorDlgSize, sizeof(SIZE));

				hKey4->WriteIntValueToReg("FAttrSw", FolderAttr);
				hKey4->WriteIntValueToReg("FAttr", FolderAttrNum);

				hKey4->WriteIntValueToReg("HistNum", FileHist);

				/* Ver1.54a以前の形式のヒストリデータは削除 */
				DeleteValue(hKey4, "Hist");

				/* ヒストリの設定を保存 */
				CopyDefaultHistory(&DefaultHist);
				n = 0;
				for(i = AskHistoryNum(); i > 0; i--)
				{
					if(GetHistoryByNum(i-1, &Hist) == FFFTP_SUCCESS)
					{
						sprintf(Str, "History%d", n);
						if(CreateSubKey(hKey4, Str, &hKey5) == FFFTP_SUCCESS)
						{
							SaveStr(hKey5, "HostAdrs", Hist.HostAdrs, DefaultHist.HostAdrs);
							SaveStr(hKey5, "UserName", Hist.UserName, DefaultHist.UserName);
							SaveStr(hKey5, "Account", Hist.Account, DefaultHist.Account);
							SaveStr(hKey5, "LocalDir", Hist.LocalInitDir, NULL);
							SaveStr(hKey5, "RemoteDir", Hist.RemoteInitDir, DefaultHist.RemoteInitDir);
							SaveStr(hKey5, "Chmod", Hist.ChmodCmd, DefaultHist.ChmodCmd);
							SaveStr(hKey5, "Nlst", Hist.LsName, DefaultHist.LsName);
							SaveStr(hKey5, "Init", Hist.InitCmd, DefaultHist.InitCmd);
							EncodePassword(Hist.PassWord, Str);
							SaveStr(hKey5, "Password", Str, DefaultHist.PassWord);
							SaveIntNum(hKey5, "Port", Hist.Port, DefaultHist.Port);
							SaveIntNum(hKey5, "Kanji", Hist.KanjiCode, DefaultHist.KanjiCode);
							SaveIntNum(hKey5, "KanaCnv", Hist.KanaCnv, DefaultHist.KanaCnv);
							SaveIntNum(hKey5, "NameKanji", Hist.NameKanjiCode, DefaultHist.NameKanjiCode);
							SaveIntNum(hKey5, "NameKana", Hist.NameKanaCnv, DefaultHist.NameKanaCnv);
							SaveIntNum(hKey5, "Pasv", Hist.Pasv, DefaultHist.Pasv);
							SaveIntNum(hKey5, "Fwall", Hist.FireWall, DefaultHist.FireWall);
							SaveIntNum(hKey5, "List", Hist.ListCmdOnly, DefaultHist.ListCmdOnly);
							SaveIntNum(hKey5, "NLST-R", Hist.UseNLST_R, DefaultHist.UseNLST_R);
							SaveIntNum(hKey5, "Tzone", Hist.TimeZone, DefaultHist.TimeZone);
							SaveIntNum(hKey5, "Type", Hist.HostType, DefaultHist.HostType);
							SaveIntNum(hKey5, "Sync", Hist.SyncMove, DefaultHist.SyncMove);
							SaveIntNum(hKey5, "Fpath", Hist.NoFullPath, DefaultHist.NoFullPath);
							hKey5->WriteBinaryToReg("Sort", &Hist.Sort, sizeof(Hist.Sort));
							SaveIntNum(hKey5, "Secu", Hist.Security, DefaultHist.Security);
							hKey5->WriteIntValueToReg("TrType", Hist.Type);
							SaveIntNum(hKey5, "Dial", Hist.Dialup, DefaultHist.Dialup);
							SaveIntNum(hKey5, "UseIt", Hist.DialupAlways, DefaultHist.DialupAlways);
							SaveIntNum(hKey5, "Notify", Hist.DialupNotify, DefaultHist.DialupNotify);
							SaveStr(hKey5, "DialTo", Hist.DialEntry, DefaultHist.DialEntry);
							// 暗号化通信対応
							SaveIntNum(hKey5, "NoEncryption", Hist.UseNoEncryption, DefaultHist.UseNoEncryption);
							SaveIntNum(hKey5, "FTPES", Hist.UseFTPES, DefaultHist.UseFTPES);
							SaveIntNum(hKey5, "FTPIS", Hist.UseFTPIS, DefaultHist.UseFTPIS);
							SaveIntNum(hKey5, "SFTP", Hist.UseSFTP, DefaultHist.UseSFTP);
							EncodePassword(Hist.PrivateKey, Str);
							SaveStr(hKey5, "PKey", Str, DefaultHist.PrivateKey);
							// 同時接続対応
							SaveIntNum(hKey5, "ThreadCount", Hist.MaxThreadCount, DefaultHist.MaxThreadCount);
							SaveIntNum(hKey5, "ReuseCmdSkt", Hist.ReuseCmdSkt, DefaultHist.ReuseCmdSkt);
							// MLSD対応
							SaveIntNum(hKey5, "MLSD", Hist.UseMLSD, DefaultHist.UseMLSD);
							// 自動切断対策
							SaveIntNum(hKey5, "Noop", Hist.NoopInterval, DefaultHist.NoopInterval);
							// 再転送対応
							SaveIntNum(hKey5, "ErrMode", Hist.TransferErrorMode, DefaultHist.TransferErrorMode);
							SaveIntNum(hKey5, "ErrNotify", Hist.TransferErrorNotify, DefaultHist.TransferErrorNotify);
							// セッションあたりの転送量制限対策
							SaveIntNum(hKey5, "ErrReconnect", Hist.TransferErrorReconnect, DefaultHist.TransferErrorReconnect);
							// ホスト側の設定ミス対策
							SaveIntNum(hKey5, "NoPasvAdrs", Hist.NoPasvAdrs, DefaultHist.NoPasvAdrs);

							CloseSubKey(hKey5);
							n++;
						}
					}
				}
				hKey4->WriteIntValueToReg("SavedHist", n);

				/* 余分なヒストリがあったら削除 */
				for(; n < 999; n++)
				{
					sprintf(Str, "History%d", n);
					if(DeleteSubKey(hKey4, Str) != FFFTP_SUCCESS)
						break;
				}

				// ホスト共通設定機能
				if(CreateSubKey(hKey4, "DefaultHost", &hKey5) == FFFTP_SUCCESS)
				{
					CopyDefaultDefaultHost(&DefaultHost);
					CopyDefaultHost(&Host);
					hKey5->WriteIntValueToReg("Set", Host.Level);
					SaveStr(hKey5, "HostName", Host.HostName, DefaultHost.HostName);
					SaveStr(hKey5, "HostAdrs", Host.HostAdrs, DefaultHost.HostAdrs);
					SaveStr(hKey5, "UserName", Host.UserName, DefaultHost.UserName);
					SaveStr(hKey5, "Account", Host.Account, DefaultHost.Account);
					SaveStr(hKey5, "LocalDir", Host.LocalInitDir, NULL);
					SaveStr(hKey5, "RemoteDir", Host.RemoteInitDir, DefaultHost.RemoteInitDir);
					SaveStr(hKey5, "Chmod", Host.ChmodCmd, DefaultHost.ChmodCmd);
					SaveStr(hKey5, "Nlst", Host.LsName, DefaultHost.LsName);
					SaveStr(hKey5, "Init", Host.InitCmd, DefaultHost.InitCmd);
					if(Host.Anonymous == NO)
						EncodePassword(Host.PassWord, Str);
					else
						strcpy(Str, DefaultHost.PassWord);
					SaveStr(hKey5, "Password", Str, DefaultHost.PassWord);
					SaveIntNum(hKey5, "Port", Host.Port, DefaultHost.Port);
					SaveIntNum(hKey5, "Anonymous", Host.Anonymous, DefaultHost.Anonymous);
					SaveIntNum(hKey5, "Kanji", Host.KanjiCode, DefaultHost.KanjiCode);
					SaveIntNum(hKey5, "KanaCnv", Host.KanaCnv, DefaultHost.KanaCnv);
					SaveIntNum(hKey5, "NameKanji", Host.NameKanjiCode, DefaultHost.NameKanjiCode);
					SaveIntNum(hKey5, "NameKana", Host.NameKanaCnv, DefaultHost.NameKanaCnv);
					SaveIntNum(hKey5, "Pasv", Host.Pasv, DefaultHost.Pasv);
					SaveIntNum(hKey5, "Fwall", Host.FireWall, DefaultHost.FireWall);
					SaveIntNum(hKey5, "List", Host.ListCmdOnly, DefaultHost.ListCmdOnly);
					SaveIntNum(hKey5, "NLST-R", Host.UseNLST_R, DefaultHost.UseNLST_R);
					SaveIntNum(hKey5, "Last", Host.LastDir, DefaultHost.LastDir);
					SaveIntNum(hKey5, "Tzone", Host.TimeZone, DefaultHost.TimeZone);
					SaveIntNum(hKey5, "Type", Host.HostType, DefaultHost.HostType);
					SaveIntNum(hKey5, "Sync", Host.SyncMove, DefaultHost.SyncMove);
					SaveIntNum(hKey5, "Fpath", Host.NoFullPath, DefaultHost.NoFullPath);
					hKey5->WriteBinaryToReg("Sort", &Host.Sort, sizeof(Host.Sort));
					SaveIntNum(hKey5, "Secu", Host.Security, DefaultHost.Security);
					hKey5->WriteMultiStringToReg("Bmarks", Host.BookMark);
					SaveIntNum(hKey5, "Dial", Host.Dialup, DefaultHost.Dialup);
					SaveIntNum(hKey5, "UseIt", Host.DialupAlways, DefaultHost.DialupAlways);
					SaveIntNum(hKey5, "Notify", Host.DialupNotify, DefaultHost.DialupNotify);
					SaveStr(hKey5, "DialTo", Host.DialEntry, DefaultHost.DialEntry);
					SaveIntNum(hKey5, "NoEncryption", Host.UseNoEncryption, DefaultHost.UseNoEncryption);
					SaveIntNum(hKey5, "FTPES", Host.UseFTPES, DefaultHost.UseFTPES);
					SaveIntNum(hKey5, "FTPIS", Host.UseFTPIS, DefaultHost.UseFTPIS);
					SaveIntNum(hKey5, "SFTP", Host.UseSFTP, DefaultHost.UseSFTP);
					EncodePassword(Host.PrivateKey, Str);
					SaveStr(hKey5, "PKey", Str, DefaultHost.PrivateKey);
					SaveIntNum(hKey5, "ThreadCount", Host.MaxThreadCount, DefaultHost.MaxThreadCount);
					SaveIntNum(hKey5, "ReuseCmdSkt", Host.ReuseCmdSkt, DefaultHost.ReuseCmdSkt);
					SaveIntNum(hKey5, "MLSD", Host.UseMLSD, DefaultHost.UseMLSD);
					SaveIntNum(hKey5, "Noop", Host.NoopInterval, DefaultHost.NoopInterval);
					SaveIntNum(hKey5, "ErrMode", Host.TransferErrorMode, DefaultHost.TransferErrorMode);
					SaveIntNum(hKey5, "ErrNotify", Host.TransferErrorNotify, DefaultHost.TransferErrorNotify);
					SaveIntNum(hKey5, "ErrReconnect", Host.TransferErrorReconnect, DefaultHost.TransferErrorReconnect);
					SaveIntNum(hKey5, "NoPasvAdrs", Host.NoPasvAdrs, DefaultHost.NoPasvAdrs);
					CloseSubKey(hKey5);
				}

				/* ホストの設定を保存 */
				CopyDefaultHost(&DefaultHost);
				i = 0;
				while(CopyHostFromList(i, &Host) == FFFTP_SUCCESS)
				{
					sprintf(Str, "Host%d", i);
					if(CreateSubKey(hKey4, Str, &hKey5) == FFFTP_SUCCESS)
					{
//						SaveIntNum(hKey5, "Set", Host.Level, DefaultHost.Level);
						hKey5->WriteIntValueToReg("Set", Host.Level);
						SaveStr(hKey5, "HostName", Host.HostName, DefaultHost.HostName);
						if((Host.Level & SET_LEVEL_GROUP) == 0)
						{
							SaveStr(hKey5, "HostAdrs", Host.HostAdrs, DefaultHost.HostAdrs);
							SaveStr(hKey5, "UserName", Host.UserName, DefaultHost.UserName);
							SaveStr(hKey5, "Account", Host.Account, DefaultHost.Account);
							SaveStr(hKey5, "LocalDir", Host.LocalInitDir, NULL);
							SaveStr(hKey5, "RemoteDir", Host.RemoteInitDir, DefaultHost.RemoteInitDir);
							SaveStr(hKey5, "Chmod", Host.ChmodCmd, DefaultHost.ChmodCmd);
							SaveStr(hKey5, "Nlst", Host.LsName, DefaultHost.LsName);
							SaveStr(hKey5, "Init", Host.InitCmd, DefaultHost.InitCmd);

							if(Host.Anonymous == NO)
								EncodePassword(Host.PassWord, Str);
							else
								strcpy(Str, DefaultHost.PassWord);
							SaveStr(hKey5, "Password", Str, DefaultHost.PassWord);

							SaveIntNum(hKey5, "Port", Host.Port, DefaultHost.Port);
							SaveIntNum(hKey5, "Anonymous", Host.Anonymous, DefaultHost.Anonymous);
							SaveIntNum(hKey5, "Kanji", Host.KanjiCode, DefaultHost.KanjiCode);
							SaveIntNum(hKey5, "KanaCnv", Host.KanaCnv, DefaultHost.KanaCnv);
							SaveIntNum(hKey5, "NameKanji", Host.NameKanjiCode, DefaultHost.NameKanjiCode);
							SaveIntNum(hKey5, "NameKana", Host.NameKanaCnv, DefaultHost.NameKanaCnv);
							SaveIntNum(hKey5, "Pasv", Host.Pasv, DefaultHost.Pasv);
							SaveIntNum(hKey5, "Fwall", Host.FireWall, DefaultHost.FireWall);
							SaveIntNum(hKey5, "List", Host.ListCmdOnly, DefaultHost.ListCmdOnly);
							SaveIntNum(hKey5, "NLST-R", Host.UseNLST_R, DefaultHost.UseNLST_R);
							SaveIntNum(hKey5, "Last", Host.LastDir, DefaultHost.LastDir);
							SaveIntNum(hKey5, "Tzone", Host.TimeZone, DefaultHost.TimeZone);
							SaveIntNum(hKey5, "Type", Host.HostType, DefaultHost.HostType);
							SaveIntNum(hKey5, "Sync", Host.SyncMove, DefaultHost.SyncMove);
							SaveIntNum(hKey5, "Fpath", Host.NoFullPath, DefaultHost.NoFullPath);
							hKey5->WriteBinaryToReg("Sort", &Host.Sort, sizeof(Host.Sort));
							SaveIntNum(hKey5, "Secu", Host.Security, DefaultHost.Security);

							hKey5->WriteMultiStringToReg("Bmarks", Host.BookMark);

							SaveIntNum(hKey5, "Dial", Host.Dialup, DefaultHost.Dialup);
							SaveIntNum(hKey5, "UseIt", Host.DialupAlways, DefaultHost.DialupAlways);
							SaveIntNum(hKey5, "Notify", Host.DialupNotify, DefaultHost.DialupNotify);
							SaveStr(hKey5, "DialTo", Host.DialEntry, DefaultHost.DialEntry);
							// 暗号化通信対応
							SaveIntNum(hKey5, "NoEncryption", Host.UseNoEncryption, DefaultHost.UseNoEncryption);
							SaveIntNum(hKey5, "FTPES", Host.UseFTPES, DefaultHost.UseFTPES);
							SaveIntNum(hKey5, "FTPIS", Host.UseFTPIS, DefaultHost.UseFTPIS);
							SaveIntNum(hKey5, "SFTP", Host.UseSFTP, DefaultHost.UseSFTP);
							EncodePassword(Host.PrivateKey, Str);
							SaveStr(hKey5, "PKey", Str, DefaultHost.PrivateKey);
							// 同時接続対応
							SaveIntNum(hKey5, "ThreadCount", Host.MaxThreadCount, DefaultHost.MaxThreadCount);
							SaveIntNum(hKey5, "ReuseCmdSkt", Host.ReuseCmdSkt, DefaultHost.ReuseCmdSkt);
							// MLSD対応
							SaveIntNum(hKey5, "MLSD", Host.UseMLSD, DefaultHost.UseMLSD);
							// 自動切断対策
							SaveIntNum(hKey5, "Noop", Host.NoopInterval, DefaultHost.NoopInterval);
							// 再転送対応
							SaveIntNum(hKey5, "ErrMode", Host.TransferErrorMode, DefaultHost.TransferErrorMode);
							SaveIntNum(hKey5, "ErrNotify", Host.TransferErrorNotify, DefaultHost.TransferErrorNotify);
							// セッションあたりの転送量制限対策
							SaveIntNum(hKey5, "ErrReconnect", Host.TransferErrorReconnect, DefaultHost.TransferErrorReconnect);
							// ホスト側の設定ミス対策
							SaveIntNum(hKey5, "NoPasvAdrs", Host.NoPasvAdrs, DefaultHost.NoPasvAdrs);
						}
						CloseSubKey(hKey5);
					}
					i++;
				}
				hKey4->WriteIntValueToReg("SetNum", i);

				/* 余分なホストの設定があったら削除 */
				for(; i < 998; i++)
				{
					sprintf(Str, "Host%d", i);
					if(DeleteSubKey(hKey4, Str) != FFFTP_SUCCESS)
						break;
				}

				if((i = AskCurrentHost()) == HOSTNUM_NOENTRY)
					i = 0;
				hKey4->WriteIntValueToReg("CurSet", i);

				// ファイルアイコン表示対応
				hKey4->WriteIntValueToReg("ListIcon", DispFileIcon);
				// タイムスタンプのバグ修正
				hKey4->WriteIntValueToReg("ListSecond", DispTimeSeconds);
				// ファイルの属性を数字で表示
				hKey4->WriteIntValueToReg("ListPermitNum", DispPermissionsNumber);
				// ディレクトリ自動作成
				hKey4->WriteIntValueToReg("MakeDir", MakeAllDir);
				// UTF-8対応
				hKey4->WriteIntValueToReg("Kanji", LocalKanjiCode);
				// UPnP対応
				hKey4->WriteIntValueToReg("UPnP", UPnPEnabled);
				// ローカル側自動更新
				hKey4->WriteIntValueToReg("ListRefresh", AutoRefreshFileList);
				// 古い処理内容を消去
				hKey4->WriteIntValueToReg("OldLog", RemoveOldLog);
				// ファイル一覧バグ修正
				hKey4->WriteIntValueToReg("AbortListErr", AbortOnListError);
				// ミラーリング設定追加
				hKey4->WriteIntValueToReg("MirNoTransfer", MirrorNoTransferContents);
				// FireWall設定追加
				hKey4->WriteIntValueToReg("FwallShared", FwallNoSaveUser);
				// ゾーンID設定追加
				hKey4->WriteIntValueToReg("MarkDFile", MarkAsInternet);
			}
			CloseSubKey(hKey4);
		}
		// 全設定暗号化対応
		EncryptSettings = NO;
		if(EncryptAllSettings == YES)
		{
			if(OpenSubKey(hKey3, "Options", &hKey4) == FFFTP_SUCCESS)
			{
				for(i = 0; ; i++)
				{
					sprintf(Str, "Host%d", i);
					if(DeleteSubKey(hKey4, Str) != FFFTP_SUCCESS)
						break;
				}
				for(i = 0; ; i++)
				{
					sprintf(Str, "History%d", i);
					if(DeleteSubKey(hKey4, Str) != FFFTP_SUCCESS)
						break;
				}
				CloseSubKey(hKey4);
			}
			DeleteSubKey(hKey3, "Options");
			DeleteValue(hKey3, "CredentialSalt");
			DeleteValue(hKey3, "CredentialCheck");
		}
		else
		{
			if(OpenSubKey(hKey3, "EncryptedOptions", &hKey4) == FFFTP_SUCCESS)
			{
				for(i = 0; ; i++)
				{
					sprintf(Str, "Host%d", i);
					if(DeleteSubKey(hKey4, Str) != FFFTP_SUCCESS)
						break;
				}
				for(i = 0; ; i++)
				{
					sprintf(Str, "History%d", i);
					if(DeleteSubKey(hKey4, Str) != FFFTP_SUCCESS)
						break;
				}
				CloseSubKey(hKey4);
			}
			DeleteSubKey(hKey3, "EncryptedOptions");
			DeleteValue(hKey3, "CredentialSalt1");
			DeleteValue(hKey3, "CredentialStretch");
			DeleteValue(hKey3, "CredentialCheck1");
		}
		CloseReg(hKey3);
	}
	return;
}

/*----- レジストリ／INIファイルから設定値を呼び出す ---------------------------
*
*	この関数を複数回呼び出すと，ホスト設定が追加される．
*
*	Parameter
*		なし
*
*	Return Value
*		YES: 読み出し成功
*		NO:  読み出し失敗(設定無し)
*----------------------------------------------------------------------------*/

int LoadRegistry(void)
{
	struct Data {
		using result_t = int;
		static void OnCommand(HWND hDlg, WORD cmd, WORD id) {
			if (cmd == BN_CLICKED)
				EndDialog(hDlg, id);
		}
	};
	Config* hKey3;
	Config* hKey4;
	Config* hKey5;
	int i;
	int Sets;
	// 暗号化通信対応
//	char Str[256];	/* ASCII_EXT_LENより大きい事 */
	char Str[PRIVATE_KEY_LEN*4+1];
	char Buf[FMAX_PATH+1];
	// 全設定暗号化対応
	char Buf2[FMAX_PATH+1];
	char *Pos;
	char *Pos2;
	HOSTDATA Host;
	HISTORYDATA Hist;
	int Sts;
	int Version;

	Sts = NO;

	SetRegType(REGTYPE_INI);
	if((i = OpenReg("FFFTP", &hKey3)) != FFFTP_SUCCESS)
	{
		if(AskForceIni() == NO)
		{
			SetRegType(REGTYPE_REG);
			i = OpenReg("FFFTP", &hKey3);
		}
	}

	if(i == FFFTP_SUCCESS)
	{
//		char checkbuf[48];
		int salt = 0;
		Sts = YES;

		hKey3->ReadIntValueFromReg("Version", &Version);
		// UTF-8対応
		if(Version < 1980)
			IniKanjiCode = KANJI_SJIS;

		// 全設定暗号化対応
		if(Version >= 1990)
		{
			if(GetMasterPasswordStatus() == PASSWORD_OK)
			{
				hKey3->ReadIntValueFromReg("EncryptAll", &EncryptAllSettings);
				sprintf(Buf, "%d", EncryptAllSettings);
				hKey3->ReadStringFromReg("EncryptAllDetector", Str, 255);
				DecodePassword(Str, Buf2);
				EncryptSettings = EncryptAllSettings;
				if(strcmp(Buf, Buf2) != 0)
				{
					switch (Dialog(GetFtpInst(), corruptsettings_dlg, GetMainHwnd(), Data{}))
					{
					case IDCANCEL:
						Terminate();
						break;
					case IDABORT:
						CloseReg(hKey3);
						ClearRegistry();
						ClearIni();
						Restart();
						Terminate();
						break;
					case IDRETRY:
						EncryptSettingsError = YES;
						break;
					case IDIGNORE:
						break;
					}
				}
			}
		}

		// 全設定暗号化対応
//		if(OpenSubKey(hKey3, "Options", &hKey4) == FFFTP_SUCCESS)
		if(EncryptAllSettings == YES)
			strcpy(Str, "EncryptedOptions");
		else
			strcpy(Str, "Options");
		if(OpenSubKey(hKey3, Str, &hKey4) == FFFTP_SUCCESS)
		{
			hKey4->ReadIntValueFromReg("WinPosX", &WinPosX);
			hKey4->ReadIntValueFromReg("WinPosY", &WinPosY);
			hKey4->ReadIntValueFromReg("WinWidth", &WinWidth);
			hKey4->ReadIntValueFromReg("WinHeight", &WinHeight);
			hKey4->ReadIntValueFromReg("LocalWidth", &LocalWidth);
			/* ↓旧バージョンのバグ対策 */
			LocalWidth = std::max(0, LocalWidth);
			hKey4->ReadIntValueFromReg("TaskHeight", &TaskHeight);
			/* ↓旧バージョンのバグ対策 */
			TaskHeight = std::max(0, TaskHeight);
			hKey4->ReadBinaryFromReg("LocalColm", &LocalTabWidth, sizeof(LocalTabWidth));
			hKey4->ReadBinaryFromReg("RemoteColm", &RemoteTabWidth, sizeof(RemoteTabWidth));
			hKey4->ReadIntValueFromReg("SwCmd", &Sizing);

			hKey4->ReadStringFromReg("UserMail", UserMailAdrs, USER_MAIL_LEN+1);
			hKey4->ReadStringFromReg("Viewer", ViewerName[0], FMAX_PATH+1);
			hKey4->ReadStringFromReg("Viewer2", ViewerName[1], FMAX_PATH+1);
			hKey4->ReadStringFromReg("Viewer3", ViewerName[2], FMAX_PATH+1);

			hKey4->ReadIntValueFromReg("TrType", &TransMode);
			hKey4->ReadIntValueFromReg("Recv", &RecvMode);
			hKey4->ReadIntValueFromReg("Send", &SendMode);
			hKey4->ReadIntValueFromReg("Move", &MoveMode);
			hKey4->ReadStringFromReg("Path", DefaultLocalPath, FMAX_PATH+1);
			hKey4->ReadIntValueFromReg("Time", &SaveTimeStamp);
			hKey4->ReadIntValueFromReg("EOF", &RmEOF);
			hKey4->ReadIntValueFromReg("Scolon", &VaxSemicolon);

			hKey4->ReadIntValueFromReg("RecvEx", &ExistMode);
			hKey4->ReadIntValueFromReg("SendEx", &UpExistMode);

			hKey4->ReadIntValueFromReg("LFsort", &LocalFileSort);
			hKey4->ReadIntValueFromReg("LDsort", &LocalDirSort);
			hKey4->ReadIntValueFromReg("RFsort", &RemoteFileSort);
			hKey4->ReadIntValueFromReg("RDsort", &RemoteDirSort);
			hKey4->ReadIntValueFromReg("SortSave", &SortSave);

			hKey4->ReadIntValueFromReg("ListType", &ListType);
			hKey4->ReadIntValueFromReg("DotFile", &DotFile);
			hKey4->ReadIntValueFromReg("Dclick", &DclickOpen);

			hKey4->ReadIntValueFromReg("ConS", &ConnectOnStart);
			hKey4->ReadIntValueFromReg("OldDlg", &ConnectAndSet);
			hKey4->ReadIntValueFromReg("RasClose", &RasClose);
			hKey4->ReadIntValueFromReg("RasNotify", &RasCloseNotify);
			hKey4->ReadIntValueFromReg("Qanony", &QuickAnonymous);
			hKey4->ReadIntValueFromReg("PassHist", &PassToHist);
			hKey4->ReadIntValueFromReg("SendQuit", &SendQuit);
			hKey4->ReadIntValueFromReg("NoRas", &NoRasControl);

			hKey4->ReadIntValueFromReg("Debug", &DebugConsole);
			hKey4->ReadIntValueFromReg("WinPos", &SaveWinPos);
			hKey4->ReadIntValueFromReg("RegExp", &FindMode);
			hKey4->ReadIntValueFromReg("Reg", &RegType);

			if(hKey4->ReadMultiStringFromReg("AsciiFile", AsciiExt, ASCII_EXT_LEN+1) == FFFTP_FAIL)
			{
				/* 旧ASCIIモードの拡張子の設定を新しいものに変換 */
				Str[0] = NUL;
				if(hKey4->ReadStringFromReg("Ascii", Str, ASCII_EXT_LEN+1) == FFFTP_SUCCESS)
					memset(AsciiExt, NUL, ASCII_EXT_LEN+1);
				Pos = Str;
				while(*Pos != NUL)
				{
					if((Pos2 = strchr(Pos, ';')) == NULL)
						Pos2 = strchr(Pos, NUL);
					if((Pos2 - Pos) > 0)
					{
						if((StrMultiLen(AsciiExt) + (Pos2 - Pos) + 2) >= ASCII_EXT_LEN)
							break;
						strcpy(AsciiExt + StrMultiLen(AsciiExt), "*.");
						strncpy(AsciiExt + StrMultiLen(AsciiExt) - 1, Pos, (Pos2 - Pos));
					}
					Pos = Pos2;
					if(*Pos == ';')
						Pos++;
				}
			}
			// アスキーモード判別の改良
			if(Version < 1986)
			{
				Pos = "*.js\0*.vbs\0*.css\0*.rss\0*.rdf\0*.xml\0*.xhtml\0*.xht\0*.shtml\0*.shtm\0*.sh\0*.py\0*.rb\0*.properties\0*.sql\0*.asp\0*.aspx\0*.php\0*.htaccess\0";
				while(*Pos != NUL)
				{
					Pos2 = AsciiExt;
					while(*Pos2 != NUL)
					{
						if(_stricmp(Pos2, Pos) == 0)
							break;
						Pos2 = strchr(Pos2, NUL) + 1;
					}
					if(*Pos2 == NUL)
					{
						if((StrMultiLen(AsciiExt) + strlen(Pos) + 2) < ASCII_EXT_LEN)
							strncpy(AsciiExt + StrMultiLen(AsciiExt), Pos, strlen(Pos) + 2);
					}
					Pos = strchr(Pos, NUL) + 1;
				}
			}

			hKey4->ReadIntValueFromReg("LowUp", &FnameCnv);
			hKey4->ReadIntValueFromReg("Tout", &TimeOut);

			hKey4->ReadMultiStringFromReg("NoTrn", MirrorNoTrn, MIRROR_LEN+1);
			hKey4->ReadMultiStringFromReg("NoDel", MirrorNoDel, MIRROR_LEN+1);
			hKey4->ReadIntValueFromReg("MirFile", &MirrorFnameCnv);
			hKey4->ReadIntValueFromReg("MirUNot", &MirUpDelNotify);
			hKey4->ReadIntValueFromReg("MirDNot", &MirDownDelNotify);

			if (hKey4->ReadStringFromReg("ListFont", Str, 256) == FFFTP_SUCCESS) {
				if (auto logfont = RestoreFontData(u8(Str).c_str())) {
					ListLogFont = *logfont;
					ListFont = CreateFontIndirectW(&ListLogFont);
				} else
					ListLogFont = {};
			}
			hKey4->ReadIntValueFromReg("ListHide", &DispIgnoreHide);
			hKey4->ReadIntValueFromReg("ListDrv", &DispDrives);

			hKey4->ReadStringFromReg("FwallHost", FwallHost, HOST_ADRS_LEN+1);
			hKey4->ReadStringFromReg("FwallUser", FwallUser, USER_NAME_LEN+1);
			hKey4->ReadStringFromReg("FwallPass", Str, 255);
			DecodePassword(Str, FwallPass);
			hKey4->ReadIntValueFromReg("FwallPort", &FwallPort);
			hKey4->ReadIntValueFromReg("FwallType", &FwallType);
			hKey4->ReadIntValueFromReg("FwallDef", &FwallDefault);
			hKey4->ReadIntValueFromReg("FwallSec", &FwallSecurity);
			hKey4->ReadIntValueFromReg("PasvDef", &PasvDefault);
			hKey4->ReadIntValueFromReg("FwallRes", &FwallResolve);
			hKey4->ReadIntValueFromReg("FwallLow", &FwallLower);
			hKey4->ReadIntValueFromReg("FwallDel", &FwallDelimiter);

			hKey4->ReadIntValueFromReg("SndConSw", &Sound[SND_CONNECT].On);
			hKey4->ReadIntValueFromReg("SndTrnSw", &Sound[SND_TRANS].On);
			hKey4->ReadIntValueFromReg("SndErrSw", &Sound[SND_ERROR].On);
			hKey4->ReadStringFromReg("SndCon", Sound[SND_CONNECT].Fname, FMAX_PATH+1);
			hKey4->ReadStringFromReg("SndTrn", Sound[SND_TRANS].Fname, FMAX_PATH+1);
			hKey4->ReadStringFromReg("SndErr", Sound[SND_ERROR].Fname, FMAX_PATH+1);

			hKey4->ReadMultiStringFromReg("DefAttr", DefAttrList, DEFATTRLIST_LEN+1);

			hKey4->ReadBinaryFromReg("Hdlg", &HostDlgSize, sizeof(SIZE));
			hKey4->ReadBinaryFromReg("Bdlg", &BmarkDlgSize, sizeof(SIZE));
			hKey4->ReadBinaryFromReg("Mdlg", &MirrorDlgSize, sizeof(SIZE));

			hKey4->ReadIntValueFromReg("FAttrSw", &FolderAttr);
			hKey4->ReadIntValueFromReg("FAttr", &FolderAttrNum);

			hKey4->ReadIntValueFromReg("NoSave", &SuppressSave);

			hKey4->ReadIntValueFromReg("HistNum", &FileHist);

			/* ヒストリの設定を読み込む */
			Sets = 0;
			hKey4->ReadIntValueFromReg("SavedHist", &Sets);

			for(i = 0; i < Sets; i++)
			{
				sprintf(Str, "History%d", i);
				if(OpenSubKey(hKey4, Str, &hKey5) == FFFTP_SUCCESS)
				{
					CopyDefaultHistory(&Hist);

					hKey5->ReadStringFromReg("HostAdrs", Hist.HostAdrs, HOST_ADRS_LEN+1);
					hKey5->ReadStringFromReg("UserName", Hist.UserName, USER_NAME_LEN+1);
					hKey5->ReadStringFromReg("Account", Hist.Account, ACCOUNT_LEN+1);
					hKey5->ReadStringFromReg("LocalDir", Hist.LocalInitDir, INIT_DIR_LEN+1);
					hKey5->ReadStringFromReg("RemoteDir", Hist.RemoteInitDir, INIT_DIR_LEN+1);
					hKey5->ReadStringFromReg("Chmod", Hist.ChmodCmd, CHMOD_CMD_LEN+1);
					hKey5->ReadStringFromReg("Nlst", Hist.LsName, NLST_NAME_LEN+1);
					hKey5->ReadStringFromReg("Init", Hist.InitCmd, INITCMD_LEN+1);
					hKey5->ReadIntValueFromReg("Port", &Hist.Port);
					hKey5->ReadIntValueFromReg("Kanji", &Hist.KanjiCode);
					hKey5->ReadIntValueFromReg("KanaCnv", &Hist.KanaCnv);
					hKey5->ReadIntValueFromReg("NameKanji", &Hist.NameKanjiCode);
					hKey5->ReadIntValueFromReg("NameKana", &Hist.NameKanaCnv);
					hKey5->ReadIntValueFromReg("Pasv", &Hist.Pasv);
					hKey5->ReadIntValueFromReg("Fwall", &Hist.FireWall);
					hKey5->ReadIntValueFromReg("List", &Hist.ListCmdOnly);
					hKey5->ReadIntValueFromReg("NLST-R", &Hist.UseNLST_R);
					hKey5->ReadIntValueFromReg("Tzone", &Hist.TimeZone);
					hKey5->ReadIntValueFromReg("Type", &Hist.HostType);
					hKey5->ReadIntValueFromReg("Sync", &Hist.SyncMove);
					hKey5->ReadIntValueFromReg("Fpath", &Hist.NoFullPath);
					hKey5->ReadBinaryFromReg("Sort", &Hist.Sort, sizeof(Hist.Sort));
					hKey5->ReadIntValueFromReg("Secu", &Hist.Security);
					hKey5->ReadIntValueFromReg("TrType", &Hist.Type);
					strcpy(Str, "");
					hKey5->ReadStringFromReg("Password", Str, 255);
					DecodePassword(Str, Hist.PassWord);
					hKey5->ReadIntValueFromReg("Dial", &Hist.Dialup);
					hKey5->ReadIntValueFromReg("UseIt", &Hist.DialupAlways);
					hKey5->ReadIntValueFromReg("Notify", &Hist.DialupNotify);
					hKey5->ReadStringFromReg("DialTo", Hist.DialEntry, RAS_NAME_LEN+1);
					// 暗号化通信対応
					hKey5->ReadIntValueFromReg("NoEncryption", &Hist.UseNoEncryption);
					hKey5->ReadIntValueFromReg("FTPES", &Hist.UseFTPES);
					hKey5->ReadIntValueFromReg("FTPIS", &Hist.UseFTPIS);
					hKey5->ReadIntValueFromReg("SFTP", &Hist.UseSFTP);
					strcpy(Str, "");
					hKey5->ReadStringFromReg("PKey", Str, PRIVATE_KEY_LEN*4+1);
					DecodePassword(Str, Hist.PrivateKey);
					// 同時接続対応
					hKey5->ReadIntValueFromReg("ThreadCount", &Hist.MaxThreadCount);
					hKey5->ReadIntValueFromReg("ReuseCmdSkt", &Hist.ReuseCmdSkt);
					// MLSD対応
					hKey5->ReadIntValueFromReg("MLSD", &Hist.UseMLSD);
					// 自動切断対策
					hKey5->ReadIntValueFromReg("Noop", &Hist.NoopInterval);
					// 再転送対応
					hKey5->ReadIntValueFromReg("ErrMode", &Hist.TransferErrorMode);
					hKey5->ReadIntValueFromReg("ErrNotify", &Hist.TransferErrorNotify);
					// セッションあたりの転送量制限対策
					hKey5->ReadIntValueFromReg("ErrReconnect", &Hist.TransferErrorReconnect);
					// ホスト側の設定ミス対策
					hKey5->ReadIntValueFromReg("NoPasvAdrs", &Hist.NoPasvAdrs);

					CloseSubKey(hKey5);
					AddHistoryToHistory(&Hist);
				}
			}

			// ホスト共通設定機能
			if(OpenSubKey(hKey4, "DefaultHost", &hKey5) == FFFTP_SUCCESS)
			{
				CopyDefaultDefaultHost(&Host);
				hKey5->ReadIntValueFromReg("Set", &Host.Level);
				hKey5->ReadStringFromReg("HostName", Host.HostName, HOST_NAME_LEN+1);
				hKey5->ReadStringFromReg("HostAdrs", Host.HostAdrs, HOST_ADRS_LEN+1);
				hKey5->ReadStringFromReg("UserName", Host.UserName, USER_NAME_LEN+1);
				hKey5->ReadStringFromReg("Account", Host.Account, ACCOUNT_LEN+1);
				hKey5->ReadStringFromReg("LocalDir", Host.LocalInitDir, INIT_DIR_LEN+1);
				hKey5->ReadStringFromReg("RemoteDir", Host.RemoteInitDir, INIT_DIR_LEN+1);
				hKey5->ReadStringFromReg("Chmod", Host.ChmodCmd, CHMOD_CMD_LEN+1);
				hKey5->ReadStringFromReg("Nlst", Host.LsName, NLST_NAME_LEN+1);
				hKey5->ReadStringFromReg("Init", Host.InitCmd, INITCMD_LEN+1);
				hKey5->ReadIntValueFromReg("Port", &Host.Port);
				hKey5->ReadIntValueFromReg("Anonymous", &Host.Anonymous);
				hKey5->ReadIntValueFromReg("Kanji", &Host.KanjiCode);
				hKey5->ReadIntValueFromReg("KanaCnv", &Host.KanaCnv);
				hKey5->ReadIntValueFromReg("NameKanji", &Host.NameKanjiCode);
				hKey5->ReadIntValueFromReg("NameKana", &Host.NameKanaCnv);
				hKey5->ReadIntValueFromReg("Pasv", &Host.Pasv);
				hKey5->ReadIntValueFromReg("Fwall", &Host.FireWall);
				hKey5->ReadIntValueFromReg("List", &Host.ListCmdOnly);
				hKey5->ReadIntValueFromReg("NLST-R", &Host.UseNLST_R);
				hKey5->ReadIntValueFromReg("Last", &Host.LastDir);
				hKey5->ReadIntValueFromReg("Tzone", &Host.TimeZone);
				hKey5->ReadIntValueFromReg("Type", &Host.HostType);
				hKey5->ReadIntValueFromReg("Sync", &Host.SyncMove);
				hKey5->ReadIntValueFromReg("Fpath", &Host.NoFullPath);
				hKey5->ReadBinaryFromReg("Sort", &Host.Sort, sizeof(Host.Sort));
				hKey5->ReadIntValueFromReg("Secu", &Host.Security);
				if(Host.Anonymous != YES)
				{
					strcpy(Str, "");
					hKey5->ReadStringFromReg("Password", Str, 255);
					DecodePassword(Str, Host.PassWord);
				}
				else
					strcpy(Host.PassWord, UserMailAdrs);
				hKey5->ReadMultiStringFromReg("Bmarks", Host.BookMark, BOOKMARK_SIZE);
				hKey5->ReadIntValueFromReg("Dial", &Host.Dialup);
				hKey5->ReadIntValueFromReg("UseIt", &Host.DialupAlways);
				hKey5->ReadIntValueFromReg("Notify", &Host.DialupNotify);
				hKey5->ReadStringFromReg("DialTo", Host.DialEntry, RAS_NAME_LEN+1);
				hKey5->ReadIntValueFromReg("NoEncryption", &Host.UseNoEncryption);
				hKey5->ReadIntValueFromReg("FTPES", &Host.UseFTPES);
				hKey5->ReadIntValueFromReg("FTPIS", &Host.UseFTPIS);
				hKey5->ReadIntValueFromReg("SFTP", &Host.UseSFTP);
				strcpy(Str, "");
				hKey5->ReadStringFromReg("PKey", Str, PRIVATE_KEY_LEN*4+1);
				DecodePassword(Str, Host.PrivateKey);
				hKey5->ReadIntValueFromReg("ThreadCount", &Host.MaxThreadCount);
				hKey5->ReadIntValueFromReg("ReuseCmdSkt", &Host.ReuseCmdSkt);
				hKey5->ReadIntValueFromReg("MLSD", &Host.UseMLSD);
				hKey5->ReadIntValueFromReg("Noop", &Host.NoopInterval);
				hKey5->ReadIntValueFromReg("ErrMode", &Host.TransferErrorMode);
				hKey5->ReadIntValueFromReg("ErrNotify", &Host.TransferErrorNotify);
				hKey5->ReadIntValueFromReg("ErrReconnect", &Host.TransferErrorReconnect);
				hKey5->ReadIntValueFromReg("NoPasvAdrs", &Host.NoPasvAdrs);

				CloseSubKey(hKey5);

				SetDefaultHost(&Host);
			}

			/* ホストの設定を読み込む */
			Sets = 0;
			hKey4->ReadIntValueFromReg("SetNum", &Sets);

			for(i = 0; i < Sets; i++)
			{
				sprintf(Str, "Host%d", i);
				if(OpenSubKey(hKey4, Str, &hKey5) == FFFTP_SUCCESS)
				{
					CopyDefaultHost(&Host);
					/* 下位互換性のため */
					// SourceForge.JPによるフォーク
//					if(Version < VER_NUM)
					if(Version < 1921)
					{
						Host.Pasv = NO;
						Host.ListCmdOnly = NO;
					}
					// 1.97b以前はデフォルトでShift_JIS
					if(Version < 1980)
						Host.NameKanjiCode = KANJI_SJIS;
					hKey5->ReadIntValueFromReg("Set", &Host.Level);

					hKey5->ReadStringFromReg("HostName", Host.HostName, HOST_NAME_LEN+1);
					hKey5->ReadStringFromReg("HostAdrs", Host.HostAdrs, HOST_ADRS_LEN+1);
					hKey5->ReadStringFromReg("UserName", Host.UserName, USER_NAME_LEN+1);
					hKey5->ReadStringFromReg("Account", Host.Account, ACCOUNT_LEN+1);
					hKey5->ReadStringFromReg("LocalDir", Host.LocalInitDir, INIT_DIR_LEN+1);
					hKey5->ReadStringFromReg("RemoteDir", Host.RemoteInitDir, INIT_DIR_LEN+1);
					hKey5->ReadStringFromReg("Chmod", Host.ChmodCmd, CHMOD_CMD_LEN+1);
					hKey5->ReadStringFromReg("Nlst", Host.LsName, NLST_NAME_LEN+1);
					hKey5->ReadStringFromReg("Init", Host.InitCmd, INITCMD_LEN+1);
					hKey5->ReadIntValueFromReg("Port", &Host.Port);
					hKey5->ReadIntValueFromReg("Anonymous", &Host.Anonymous);
					hKey5->ReadIntValueFromReg("Kanji", &Host.KanjiCode);
					// 1.98b以前のUTF-8はBOMあり
					if(Version < 1983)
					{
						if(Host.KanjiCode == KANJI_UTF8N)
							Host.KanjiCode = KANJI_UTF8BOM;
					}
					hKey5->ReadIntValueFromReg("KanaCnv", &Host.KanaCnv);
					hKey5->ReadIntValueFromReg("NameKanji", &Host.NameKanjiCode);
					hKey5->ReadIntValueFromReg("NameKana", &Host.NameKanaCnv);
					hKey5->ReadIntValueFromReg("Pasv", &Host.Pasv);
					hKey5->ReadIntValueFromReg("Fwall", &Host.FireWall);
					hKey5->ReadIntValueFromReg("List", &Host.ListCmdOnly);
					hKey5->ReadIntValueFromReg("NLST-R", &Host.UseNLST_R);
					hKey5->ReadIntValueFromReg("Last", &Host.LastDir);
					hKey5->ReadIntValueFromReg("Tzone", &Host.TimeZone);
					hKey5->ReadIntValueFromReg("Type", &Host.HostType);
					hKey5->ReadIntValueFromReg("Sync", &Host.SyncMove);
					hKey5->ReadIntValueFromReg("Fpath", &Host.NoFullPath);
					hKey5->ReadBinaryFromReg("Sort", &Host.Sort, sizeof(Host.Sort));
					hKey5->ReadIntValueFromReg("Secu", &Host.Security);
					if(Host.Anonymous != YES)
					{
						strcpy(Str, "");
						hKey5->ReadStringFromReg("Password", Str, 255);
						DecodePassword(Str, Host.PassWord);
					}
					else
						strcpy(Host.PassWord, UserMailAdrs);

					hKey5->ReadMultiStringFromReg("Bmarks", Host.BookMark, BOOKMARK_SIZE);

					hKey5->ReadIntValueFromReg("Dial", &Host.Dialup);
					hKey5->ReadIntValueFromReg("UseIt", &Host.DialupAlways);
					hKey5->ReadIntValueFromReg("Notify", &Host.DialupNotify);
					hKey5->ReadStringFromReg("DialTo", Host.DialEntry, RAS_NAME_LEN+1);
					// 暗号化通信対応
					hKey5->ReadIntValueFromReg("NoEncryption", &Host.UseNoEncryption);
					hKey5->ReadIntValueFromReg("FTPES", &Host.UseFTPES);
					hKey5->ReadIntValueFromReg("FTPIS", &Host.UseFTPIS);
					hKey5->ReadIntValueFromReg("SFTP", &Host.UseSFTP);
					strcpy(Str, "");
					hKey5->ReadStringFromReg("PKey", Str, PRIVATE_KEY_LEN*4+1);
					DecodePassword(Str, Host.PrivateKey);
					// 同時接続対応
					hKey5->ReadIntValueFromReg("ThreadCount", &Host.MaxThreadCount);
					hKey5->ReadIntValueFromReg("ReuseCmdSkt", &Host.ReuseCmdSkt);
					// 1.98d以前で同時接続数が1より大きい場合はソケットの再利用なし
					if(Version < 1985)
					{
						if(Host.MaxThreadCount > 1)
							Host.ReuseCmdSkt = NO;
					}
					// MLSD対応
					hKey5->ReadIntValueFromReg("MLSD", &Host.UseMLSD);
					// 自動切断対策
					hKey5->ReadIntValueFromReg("Noop", &Host.NoopInterval);
					// 再転送対応
					hKey5->ReadIntValueFromReg("ErrMode", &Host.TransferErrorMode);
					hKey5->ReadIntValueFromReg("ErrNotify", &Host.TransferErrorNotify);
					// セッションあたりの転送量制限対策
					hKey5->ReadIntValueFromReg("ErrReconnect", &Host.TransferErrorReconnect);
					// ホスト側の設定ミス対策
					hKey5->ReadIntValueFromReg("NoPasvAdrs", &Host.NoPasvAdrs);

					CloseSubKey(hKey5);

					AddHostToList(&Host, -1, Host.Level);
				}
			}

			hKey4->ReadIntValueFromReg("CurSet", &Sets);
			SetCurrentHost(Sets);

			// ファイルアイコン表示対応
			hKey4->ReadIntValueFromReg("ListIcon", &DispFileIcon);
			// タイムスタンプのバグ修正
			hKey4->ReadIntValueFromReg("ListSecond", &DispTimeSeconds);
			// ファイルの属性を数字で表示
			hKey4->ReadIntValueFromReg("ListPermitNum", &DispPermissionsNumber);
			// ディレクトリ自動作成
			hKey4->ReadIntValueFromReg("MakeDir", &MakeAllDir);
			// UTF-8対応
			hKey4->ReadIntValueFromReg("Kanji", &LocalKanjiCode);
			// UPnP対応
			hKey4->ReadIntValueFromReg("UPnP", &UPnPEnabled);
			// ローカル側自動更新
			hKey4->ReadIntValueFromReg("ListRefresh", &AutoRefreshFileList);
			// 古い処理内容を消去
			hKey4->ReadIntValueFromReg("OldLog", &RemoveOldLog);
			// ファイル一覧バグ修正
			hKey4->ReadIntValueFromReg("AbortListErr", &AbortOnListError);
			// ミラーリング設定追加
			hKey4->ReadIntValueFromReg("MirNoTransfer", &MirrorNoTransferContents);
			// FireWall設定追加
			hKey4->ReadIntValueFromReg("FwallShared", &FwallNoSaveUser);
			// ゾーンID設定追加
			hKey4->ReadIntValueFromReg("MarkDFile", &MarkAsInternet);

			CloseSubKey(hKey4);
		}
		// 全設定暗号化対応
		EncryptSettings = NO;
		CloseReg(hKey3);
	}
	else
	{
		/*===== 最初の起動時（設定が無い) =====*/
	}
	return(Sts);
}


// レジストリの設定値をクリア
void ClearRegistry() {
	SHDeleteKeyW(HKEY_CURRENT_USER, LR"(Software\Sota\FFFTP)");
}


void ClearIni() {
	fs::remove(fs::u8path(AskIniFilePath()));
}


// 設定をファイルに保存
void SaveSettingsToFile() {
	if (RegType == REGTYPE_REG) {
		if (auto const path = SelectFile(false, GetMainHwnd(), IDS_SAVE_SETTING, L"FFFTP.reg", L"reg", { FileType::Reg, FileType::All }); !std::empty(path)) {
			wchar_t commandLine[FMAX_PATH * 2];
			_snwprintf(commandLine, std::size(commandLine), LR"("%s\reg.exe" EXPORT HKCU\Software\sota\FFFTP "%s")", systemDirectory().c_str(), path.c_str());
			fs::remove(path);
			STARTUPINFOW si{ sizeof(STARTUPINFOW) };
			if (ProcessInformation pi; !CreateProcessW(nullptr, commandLine, nullptr, nullptr, false, CREATE_NO_WINDOW, nullptr, systemDirectory().c_str(), &si, &pi))
				Message(IDS_FAIL_TO_EXEC_REDEDIT, MB_OK | MB_ICONERROR);
		}
	} else {
		if (auto const path = SelectFile(false, GetMainHwnd(), IDS_SAVE_SETTING, L"FFFTP-Backup.ini", L"ini", { FileType::Ini, FileType::All }); !std::empty(path))
			CopyFileW(u8(AskIniFilePath()).c_str(), path.c_str(), FALSE);
	}
}


// 設定をファイルから復元
int LoadSettingsFromFile() {
	if (auto const path = SelectFile(true, GetMainHwnd(), IDS_LOAD_SETTING, L"", L"", { FileType::Reg, FileType::Ini, FileType::All }); !std::empty(path)) {
		if (ieq(path.extension(), L".reg"s)) {
			wchar_t commandLine[FMAX_PATH * 2];
			_snwprintf(commandLine, std::size(commandLine), LR"("%s\reg.exe" IMPORT "%s")", systemDirectory().c_str(), path.c_str());
			STARTUPINFOW si{ sizeof(STARTUPINFOW), nullptr, nullptr, nullptr, 0, 0, 0, 0, 0, 0, 0, STARTF_USESHOWWINDOW, SW_HIDE };
			if (ProcessInformation pi; CreateProcessW(nullptr, commandLine, nullptr, nullptr, false, CREATE_NO_WINDOW, nullptr, systemDirectory().c_str(), &si, &pi))
				return YES;
			Message(IDS_FAIL_TO_EXEC_REDEDIT, MB_OK | MB_ICONERROR);
		} else if (ieq(path.extension(), L".ini"s)) {
			CopyFileW(path.c_str(), u8(AskIniFilePath()).c_str(), FALSE);
			return YES;
		} else
			Message(IDS_MUST_BE_REG_OR_INI, MB_OK | MB_ICONERROR);
	}
	return NO;
}


/*----- レジストリ/INIファイルに文字列をセーブ --------------------------------
*
*	Parameter
*		HKEY hKey : レジストリキー
*		char *Key : キー名
*		char *Str : セーブする文字列
*		char *DefaultStr : デフォルトの文字列
*
*	Return Value
*		なし
*
*	Note
*		文字列がデフォルトの文字列と同じならセーブしない
*----------------------------------------------------------------------------*/

static void SaveStr(Config* Handle, char *Key, char *Str, char *DefaultStr)
{
	if((DefaultStr != NULL) && (strcmp(Str, DefaultStr) == 0))
//		DeleteValue(hKey, Key);
		DeleteValue(Handle, Key);
	else
		Handle->WriteStringToReg(Key, Str);

	return;
}


/*----- レジストリ/INIファイルに数値(INT)をセーブ -----------------------------
*
*	Parameter
*		HKEY hKey : レジストリキー
*		char *Key : キー名
*		int Num : セーブする値
*		int DefaultNum : デフォルトの値
*
*	Return Value
*		なし
*
*	Note
*		数値がデフォルトの値と同じならセーブしない
*----------------------------------------------------------------------------*/

static void SaveIntNum(Config* Handle, char *Key, int Num, int DefaultNum)
{
	if(Num == DefaultNum)
//		DeleteValue(hKey, Key);
		DeleteValue(Handle, Key);
	else
		Handle->WriteIntValueToReg(Key, Num);

	return;
}


// LOGFONTデータを文字列に変換する
static std::wstring MakeFontData(HFONT hfont, LOGFONTW const& logfont) {
	if (!hfont)
		return {};
	wchar_t buffer[1024];
	swprintf(buffer, std::size(buffer), L"%ld %ld %ld %ld %ld %hhu %hhu %hhu %hhu %hhu %hhu %hhu %hhu %s",
		logfont.lfHeight, logfont.lfWidth, logfont.lfEscapement, logfont.lfOrientation, logfont.lfWeight,
		logfont.lfItalic, logfont.lfUnderline, logfont.lfStrikeOut, logfont.lfCharSet,
		logfont.lfOutPrecision, logfont.lfClipPrecision, logfont.lfQuality, logfont.lfPitchAndFamily, logfont.lfFaceName
	);
	return buffer;
}


// 文字列をLOGFONTデータに変換する
static std::optional<LOGFONTW> RestoreFontData(const wchar_t* str) {
	LOGFONTW logfont;
	int offset;
	__pragma(warning(suppress:6328)) auto read = swscanf(str, L"%ld %ld %ld %ld %ld %hhu %hhu %hhu %hhu %hhu %hhu %hhu %hhu %n",
		&logfont.lfHeight, &logfont.lfWidth, &logfont.lfEscapement, &logfont.lfOrientation, &logfont.lfWeight,
		&logfont.lfItalic, &logfont.lfUnderline, &logfont.lfStrikeOut, &logfont.lfCharSet,
		&logfont.lfOutPrecision, &logfont.lfClipPrecision, &logfont.lfQuality, &logfont.lfPitchAndFamily, &offset);
	if (read != 13)
		return {};
	wcscpy(logfont.lfFaceName, str + offset);
	return logfont;
}

// パスワードを暗号化する(AES)
static void EncodePassword(std::string_view const& Str, char *Buf) {
	auto result = false;
	try {
		auto p = Buf;
		auto length = size_as<DWORD>(Str);
		auto paddedLength = (length + AES_BLOCK_SIZE - 1) / AES_BLOCK_SIZE * AES_BLOCK_SIZE;
		paddedLength = std::max(paddedLength, DWORD(AES_BLOCK_SIZE * 2));	/* 最低長を32文字とする */
		std::vector<BYTE> buffer(paddedLength, 0);
		std::copy(begin(Str), end(Str), begin(buffer));

		/* PAD部分を乱数で埋める StrPad[StrLen](が有効な場合) は NUL */
		if (paddedLength <= length + 1 || CryptGenRandom(HCryptProv, paddedLength - length - 1, &buffer[(size_t)length + 1]))
			// IVの初期化
			if (unsigned char iv[AES_BLOCK_SIZE]; CryptGenRandom(HCryptProv, size_as<DWORD>(iv), iv)) {
				*p++ = '0';
				*p++ = 'C';
				for (auto const& item : iv) {
					sprintf(p, "%02x", item);
					p += 2;
				}
				*p++ = ':';

				// PLAINTEXTKEYBLOB structure https://msdn.microsoft.com/en-us/library/jj650836(v=vs.85).aspx
				struct _PLAINTEXTKEYBLOB {
					BLOBHEADER hdr;
					DWORD dwKeySize;
					BYTE rgbKeyData[32];
				} keyBlob{ { PLAINTEXTKEYBLOB, CUR_BLOB_VERSION, 0, CALG_AES_256 }, 32 };
				if (CreateAesKey(keyBlob.rgbKeyData))
					if (HCRYPTKEY hkey; CryptImportKey(HCryptProv, reinterpret_cast<const BYTE*>(&keyBlob), DWORD(sizeof keyBlob), 0, 0, &hkey)) {
						if (DWORD mode = CRYPT_MODE_CBC; CryptSetKeyParam(hkey, KP_MODE, reinterpret_cast<const BYTE*>(&mode), 0))
							if (CryptSetKeyParam(hkey, KP_IV, iv, 0))
								if (CryptEncrypt(hkey, 0, false, 0, data(buffer), &paddedLength, paddedLength)) {
									for (auto const& item : buffer) {
										sprintf(p, "%02x", item);
										p += 2;
									}
									*p = NUL;
									result = true;
								}
						CryptDestroyKey(hkey);
					}
			}
	}
	catch (std::bad_alloc&) {}
	if (!result)
		Buf[0] = NUL;
}


/*----- パスワードの暗号化を解く ----------------------------------------------
*
*	Parameter
*		char *Str : 暗号化したパスワード
*		char *Buf : パスワードを格納するバッファ
*
*	Return Value
*		なし
*----------------------------------------------------------------------------*/

static void DecodePassword(char *Str, char *Buf)
{
	unsigned char *Get;
	unsigned char *Put;

	Get = (unsigned char *)Str;
	Put = (unsigned char *)Buf;
	
	if( *Get == NUL ){
		*Put = NUL;
	}
	else if( 0x40 <= *Get && *Get < 0x80 ){
		/* Original algorithm */
		DecodePasswordOriginal( Str, Buf );
	}
	else if( strncmp( (const char*)Get, "0A", 2 ) == 0 ){
		DecodePasswordOriginal( Str + 2, Buf );
	}
	else if( strncmp( (const char*)Get, "0B", 2 ) == 0 ){
		DecodePassword2( Str + 2, Buf, SecretKey );
	}
	else if( strncmp( (const char*)Get, "0C", 2 ) == 0 ){
		DecodePassword3( Str + 2, Buf );
	}
	else {
		//	unknown encoding
		*Put = NUL;
		return;
	}
}

/*----- パスワードの暗号化を解く(オリジナルアルゴリズム) -------------------
*
*	Parameter
*		char *Str : 暗号化したパスワード
*		char *Buf : パスワードを格納するバッファ
*
*	Return Value
*		なし
*----------------------------------------------------------------------------*/
static void DecodePasswordOriginal(char *Str, char *Buf)
{
	unsigned char *Get;
	unsigned char *Put;
	int Rnd;
	int Ch;

	Get = (unsigned char *)Str;
	Put = (unsigned char *)Buf;

	while(*Get != NUL)
	{
		Rnd = ((unsigned int)*Get >> 4) & 0x3;
		Ch = (*Get & 0xF) | ((*(Get+1) & 0xF) << 4);
		Ch <<= 8;
		if((*Get & 0x1) != 0)
			Get++;
		Get += 2;
		Ch >>= Rnd;
		Ch = (Ch & 0xFF) | ((Ch >> 8) & 0xFF);
		*Put++ = Ch;
	}
	*Put = NUL;
	return;
}

/*----- パスワードの暗号化を解く(オリジナルアルゴリズム＾Key) -------------------
*
*	Parameter
*		char *Str : 暗号化したパスワード
*		char *Buf : パスワードを格納するバッファ
*		const char *Key : 暗号化キー
*
*	Return Value
*		なし
*----------------------------------------------------------------------------*/
static void DecodePassword2(char *Str, char *Buf, const char* Key)
{
	int Rnd;
	int Ch;
	unsigned char *Get = (unsigned char *)Str;
	unsigned char *Put = (unsigned char *)Buf;

	/* 2010.01.31 genta Key */
	unsigned char *KeyHead = (unsigned char *)Key;
	unsigned char *KeyEnd = KeyHead + strlen((const char*)KeyHead);
	unsigned char *KeyCurrent = KeyHead;

	while(*Get != NUL)
	{
		Rnd = ((unsigned int)*Get >> 4) & 0x3;
		Ch = (*Get & 0xF) | ((*(Get+1) & 0xF) << 4);
		Ch <<= 8;
		if((*Get & 0x1) != 0)
			Get++;
		Get += 2;
		Ch >>= Rnd;
		Ch = (Ch & 0xFF) | ((Ch >> 8) & 0xFF);
		*Put++ = Ch ^ *KeyCurrent;
		
		/* 2010.01.31 genta Key */
		if( ++KeyCurrent == KeyEnd ){
			KeyCurrent = KeyHead;
		}
	}
	*Put = NUL;
	return;
}

/*----- パスワードの暗号化を解く(AES) ---------------------------------------
*
*	Parameter
*		char *Str : 暗号化したパスワード
*		char *Buf : パスワードを格納するバッファ
*		const char *Key : 暗号化キー
*
*	Return Value
*		なし
*----------------------------------------------------------------------------*/

static void DecodePassword3(char *Str, char *Buf) {
	try {
		Buf[0] = NUL;
		if (auto length = DWORD(strlen(Str)); AES_BLOCK_SIZE * 2 + 1 < length) {
			DWORD encodedLength = (length - 1) / 2 - AES_BLOCK_SIZE;
			std::vector<unsigned char> buffer((size_t)encodedLength + 1, 0);	// NUL終端用に１バイト追加
			unsigned char iv[AES_BLOCK_SIZE];
			for (auto& item : iv) {
				std::from_chars(Str, Str + 2, item, 16);
				Str += 2;
			}
			if (*Str++ == ':') {
				// PLAINTEXTKEYBLOB structure https://msdn.microsoft.com/en-us/library/jj650836(v=vs.85).aspx
				struct _PLAINTEXTKEYBLOB {
					BLOBHEADER hdr;
					DWORD dwKeySize;
					BYTE rgbKeyData[32];
				} keyBlob{ { PLAINTEXTKEYBLOB, CUR_BLOB_VERSION, 0, CALG_AES_256 }, 32 };
				if (CreateAesKey(keyBlob.rgbKeyData)) {
					for (DWORD i = 0; i < encodedLength; i++) {
						std::from_chars(Str, Str + 2, buffer[i], 16);
						Str += 2;
					}
					if (HCRYPTKEY hkey; CryptImportKey(HCryptProv, reinterpret_cast<const BYTE*>(&keyBlob), sizeof keyBlob, 0, 0, &hkey)) {
						if (DWORD mode = CRYPT_MODE_CBC; CryptSetKeyParam(hkey, KP_MODE, reinterpret_cast<const BYTE*>(&mode), 0))
							if (CryptSetKeyParam(hkey, KP_IV, iv, 0))
								if (CryptDecrypt(hkey, 0, false, 0, data(buffer), &encodedLength))
									strcpy(Buf, reinterpret_cast<const char*>(data(buffer)));
						CryptDestroyKey(hkey);
					}
				}
			}
		}
	}
	catch (std::bad_alloc&) {}
}

// AES用固定長キーを作成
// SHA-1をもちいて32Byte鍵を生成する
static bool CreateAesKey(unsigned char *AesKey) {
	char* HashKey;
	uint32_t HashKeyLen;
	uint32_t results[10];
	int ByteOffset;
	int KeyIndex;
	int ResIndex;

	HashKeyLen = (uint32_t)strlen(SecretKey) + 16;
	if((HashKey = (char*)malloc((size_t)HashKeyLen + 1)) == NULL){
		return false;
	}

	strcpy(HashKey, SecretKey);
	strcat(HashKey, ">g^r=@N7=//z<[`:");
	sha_memory(HashKey, HashKeyLen, results);

	strcpy(HashKey, SecretKey);
	strcat(HashKey, "VG77dO1#EyC]$|C@");
	sha_memory(HashKey, HashKeyLen, results + 5);

	KeyIndex = 0;
	ResIndex = 0;
	while (ResIndex < 8) {
		for (ByteOffset = 0; ByteOffset < 4; ByteOffset++) {
			AesKey[KeyIndex++] = (results[ResIndex] >> ByteOffset * 8) & 0xff;
		}
		ResIndex++;
	}
	free(HashKey);

	return true;
}


/*===== レジストリとINIファイルのアクセス処理 ============*/

struct REGDATATBL : Config {
	std::shared_ptr<std::map<std::string, std::vector<std::string>>> map;
	bool const update;
	REGDATATBL(std::string const& keyName, bool update) : Config{ keyName }, map{ new std::map<std::string, std::vector<std::string>>{} }, update{ update } {}
	REGDATATBL(std::string const& keyName, REGDATATBL& parent) : Config{ keyName }, map{ parent.map }, update{ false } {}
	~REGDATATBL() override {
		if (update) {
			std::ofstream of{ fs::u8path(AskIniFilePath()) };
			if (!of) {
				Message(IDS_CANT_SAVE_TO_INI, MB_OK | MB_ICONERROR);
				return;
			}
			of << MSGJPN239;
			for (auto const& [key, lines] : *map) {
				of << "\n[" << key << "]\n";
				for (auto const& line : lines)
					of << line << "\n";
			}
		}
	}
	const char* Scan(std::string_view name) const {
		for (auto const& line : (*map)[KeyName])
			if (size(name) + 1 < size(line) && line.starts_with(name) && line[size(name)] == '=')
				return data(line) + size(name) + 1;
		return nullptr;
	}
	std::optional<int> ReadInt(std::string_view name) const override {
		if (auto const p = Scan(name))
			return atoi(p);
		return {};
	}
	std::optional<std::string> ReadValue(std::string_view name) const override {
		static std::regex re{ R"(\\([0-9A-F]{2})|\\\\)" };
		if (auto const p = Scan(name)) {
			auto const value = replace({ p }, re, [](auto const& m) { return m[1].matched ? std::stoi(m[1], nullptr, 16) : '\\'; });
			return IniKanjiCode == KANJI_SJIS ? u8(a2w(value)) : value;
		}
		return {};
	}
	void Write(const char* name, int value) override {
		(*map)[KeyName].push_back(std::string{ name } + '=' + std::to_string(value));
	}
	void Write(const char* name, std::string_view value, DWORD) override {
		auto line = std::string{ name } +'=';
		for (auto it = begin(value); it != end(value); ++it)
			if (0x20 <= *it && *it < 0x7F) {
				if (*it == '\\')
					line += '\\';
				line += *it;
			} else {
				char buffer[4];
				sprintf(buffer, "\\%02X", (unsigned char)*it);
				line += buffer;
			}
		(*map)[KeyName].push_back(std::move(line));
	}
};

struct REGDATATBL_REG : Config {
	HKEY hKey;
	REGDATATBL_REG(std::string const& keyName, HKEY hkey) : Config{ keyName }, hKey{ hkey } {}
	~REGDATATBL_REG() override {
		RegCloseKey(hKey);
	}
	std::optional<int> ReadInt(std::string_view name) const override {
		if (DWORD value, size = sizeof(int); RegQueryValueExW(hKey, u8(name).c_str(), nullptr, nullptr, reinterpret_cast<BYTE*>(&value), &size) == ERROR_SUCCESS)
			return value;
		return {};
	}
	std::optional<std::string> ReadValue(std::string_view name) const override {
		auto const wName = u8(name);
		if (DWORD type, count; RegQueryValueExW(hKey, wName.c_str(), nullptr, &type, nullptr, &count) == ERROR_SUCCESS) {
			if (type == REG_BINARY) {
				// TODO: EncryptSettings == YESの時、末尾に\0を含むが削除していない。
				if (std::string value(count, '\0'); RegQueryValueExW(hKey, wName.c_str(), nullptr, nullptr, data_as<BYTE>(value), &count) == ERROR_SUCCESS)
					return value;
			} else {
				// TODO: 末尾に\0が含まれているが削除していない。
				assert(EncryptSettings != YES && (type == REG_SZ || type == REG_MULTI_SZ));
				if (std::wstring value(count / sizeof(wchar_t), L'\0'); RegQueryValueExW(hKey, wName.c_str(), nullptr, nullptr, data_as<BYTE>(value), &count) == ERROR_SUCCESS)
					return u8(value);
			}
		}
		return {};
	}
	void Write(const char* name, int value) override {
		RegSetValueExW(hKey, u8(name).c_str(), 0, REG_DWORD, reinterpret_cast<CONST BYTE*>(&value), sizeof(int));
	}
	void Write(const char* name, std::string_view value, DWORD type) override {
		if (EncryptSettings == YES || type == REG_BINARY)
			RegSetValueExW(hKey, u8(name).c_str(), 0, REG_BINARY, data_as<const BYTE>(value), type == REG_BINARY ? size_as<DWORD>(value) : size_as<DWORD>(value) + 1);
		else {
			auto const wvalue = u8(value);
			RegSetValueExW(hKey, u8(name).c_str(), 0, type, data_as<const BYTE>(wvalue), (size_as<DWORD>(wvalue) + 1) * sizeof(wchar_t));
		}
	}
};

/*===== プロトタイプ =====*/

static int ReadInReg(char *Name, REGDATATBL **Handle);


/*===== ローカルなワーク =====*/

static int TmpRegType;



/*----- レジストリのタイプを設定する ------------------------------------------
*
*	Parameter
*		int Type : タイプ (REGTYPE_xxx)
*
*	Return Value
*		int ステータス
*			FFFTP_SUCCESS/FFFTP_FAIL
*----------------------------------------------------------------------------*/

static void SetRegType(int Type)
{
	TmpRegType = Type;
	return;
}


// レジストリ/INIファイルをオープンする（読み込み）
static int OpenReg(char* Name, Config** Handle) {
	if (TmpRegType == REGTYPE_REG) {
		if (HKEY key; RegOpenKeyExW(HKEY_CURRENT_USER, (LR"(Software\Sota\)"sv + u8(Name)).c_str(), 0, KEY_READ, &key) == ERROR_SUCCESS) {
			*Handle = new REGDATATBL_REG{ Name, key };
			return FFFTP_SUCCESS;
		}
	} else {
		if (ReadInReg(Name, (REGDATATBL**)Handle) == FFFTP_SUCCESS)
			return FFFTP_SUCCESS;
	}
	return FFFTP_FAIL;
}


// レジストリ/INIファイルを作成する（書き込み）
static int CreateReg(char* Name, Config** Handle) {
	if (TmpRegType == REGTYPE_REG) {
		if (HKEY key; RegCreateKeyExW(HKEY_CURRENT_USER, (LR"(Software\Sota\)"sv + u8(Name)).c_str(), 0, nullptr, 0, KEY_CREATE_SUB_KEY | KEY_SET_VALUE, nullptr, &key, nullptr) == ERROR_SUCCESS) {
			*Handle = new REGDATATBL_REG{ Name, key };
			return FFFTP_SUCCESS;
		}
	} else {
		*Handle = new REGDATATBL{ Name, true };
		return FFFTP_SUCCESS;
	}
	return FFFTP_FAIL;
}


// レジストリ/INIファイルをクローズする
static void CloseReg(Config* Handle) {
	delete Handle;
}


// INIファイルからレジストリ情報を読み込む
static int ReadInReg(char* Name, REGDATATBL** Handle) {
	*Handle = nullptr;
	std::ifstream is{ fs::u8path(AskIniFilePath()) };
	if (!is)
		return FFFTP_FAIL;
	std::string key{ Name };
	auto root = new REGDATATBL{ key, false };
	*Handle = root;
	for (std::string line; getline(is, line);) {
		if (empty(line) || line[0] == '#')
			continue;
		if (line[0] == '[') {
			if (auto pos = line.find(']'); pos != std::string::npos)
				line.resize(pos);
			key = line.substr(1);
		} else
			(*root->map)[key].push_back(line);
	}
	return FFFTP_SUCCESS;
}


// サブキーをオープンする
static int OpenSubKey(Config* Parent, char* Name, Config** Handle) {
	if (TmpRegType == REGTYPE_REG) {
		if (HKEY key; RegOpenKeyExW(((REGDATATBL_REG*)Parent)->hKey, u8(Name).c_str(), 0, KEY_READ, &key) == ERROR_SUCCESS) {
			*Handle = new REGDATATBL_REG{ Parent->KeyName + '\\' + Name, key };
			return FFFTP_SUCCESS;
		}
	} else {
		if (auto const keyName = Parent->KeyName + '\\' + Name; ((REGDATATBL*)Parent)->map->contains(keyName)) {
			*Handle = new REGDATATBL{ keyName, *(REGDATATBL*)Parent };
			return FFFTP_SUCCESS;
		}
	}
	return FFFTP_FAIL;
}


// サブキーを作成する
static int CreateSubKey(Config* Parent, char* Name, Config** Handle) {
	if (TmpRegType == REGTYPE_REG) {
		if (HKEY key; RegCreateKeyExW(((REGDATATBL_REG*)Parent)->hKey, u8(Name).c_str(), 0, nullptr, 0, KEY_SET_VALUE, nullptr, &key, nullptr) == ERROR_SUCCESS) {
			*Handle = new REGDATATBL_REG{ Parent->KeyName + '\\' + Name, key };
			return FFFTP_SUCCESS;
		}
	} else {
		*Handle = new REGDATATBL{ Parent->KeyName + '\\' + Name, *(REGDATATBL*)Parent };
		return FFFTP_SUCCESS;
	}
	return FFFTP_FAIL;
}


/*----- サブキーをクローズする ------------------------------------------------
*
*	Parameter
*		void *Handle : ハンドル
*
*	Return Value
*		int ステータス
*			FFFTP_SUCCESS/FFFTP_FAIL
*----------------------------------------------------------------------------*/

static int CloseSubKey(Config* Handle)
{
	delete Handle;
	return(FFFTP_SUCCESS);
}


/*----- サブキーを削除する ----------------------------------------------------
*
*	Parameter
*		void *Handle : ハンドル
*		char *Name : 名前
*
*	Return Value
*		int ステータス
*			FFFTP_SUCCESS/FFFTP_FAIL
*----------------------------------------------------------------------------*/

static int DeleteSubKey(Config* Handle, char *Name)
{
	int Sts;

	Sts = FFFTP_FAIL;
	if(TmpRegType == REGTYPE_REG)
	{
		if(RegDeleteKeyW(((REGDATATBL_REG *)Handle)->hKey, u8(Name).c_str()) == ERROR_SUCCESS)
			Sts = FFFTP_SUCCESS;
	}
	else
	{
		Sts = FFFTP_FAIL;
	}
	return(Sts);
}


// 値を削除する
static int DeleteValue(Config* Handle, char* Name) {
	if (TmpRegType == REGTYPE_REG) {
		if (RegDeleteValueW(((REGDATATBL_REG*)Handle)->hKey, u8(Name).c_str()) == ERROR_SUCCESS)
			return FFFTP_SUCCESS;
	}
	return FFFTP_FAIL;
}


/*----------- パスワードの妥当性を確認する ------------------------------------
*
*	Parameter
*		char *Password: パスワード文字列
*		char *HashStr: SHA-1ハッシュ文字列
*
*	Return Value
*		int 0 不一致
*			1: 一致
*			2: 異常
*----------------------------------------------------------------------------*/
// 全設定暗号化対応
//int CheckPasswordValidity( char* Password, int length, const char* HashStr )
int CheckPasswordValidity( char* Password, int length, const char* HashStr, int StretchCount )
{
	char Buf[MAX_PASSWORD_LEN + 32];
	ulong hash1[5];
	uint32_t hash2[5];
	
	int i, j;
	
	const char* p = HashStr;
	
	/* 空文字列は一致したことにする */
	if( HashStr[0] == NUL )	return 1;

	/* Hashをチェックするする*/
	if( strlen(HashStr) != 40 )	return 2;

	/* Hashをデコードする*/
	for( i = 0; i < 5; i++ ){
		ulong decode = 0;
		for( j = 0; j < 8; j++ ){
			if( *p < 0x40 || 0x40 + 15 < *p ){
				return 2;
			}
			decode = (decode << 4 ) + (*p - 0x40);
			++p;
		}
		hash1[i] = decode;
	}
	
	/* Password をハッシュする */
	sha_memory( Password, length, hash2 );
	for(i = 0; i < StretchCount; i++)
	{
		memcpy(&Buf[0], &hash2, 20);
		memcpy(&Buf[20], Password, length);
		sha_memory(Buf, 20 + length, hash2);
	}
	
	if( memcmp( (char*)hash1, (char*)hash2, sizeof( hash1 )) == 0 ){
		return 1;
	}
	return 0;
}

/*----------- パスワードの妥当性チェックのための文字列を作成する ------------
*
*	Parameter
*		char *Password: パスワード文字列
*		char *Str: SHA-1ハッシュ文字列格納場所 (41bytes以上)
*
*	Return Value
*		None
*----------------------------------------------------------------------------*/
// 全設定暗号化対応
//void CreatePasswordHash( char* Password, int length, char* HashStr )
void CreatePasswordHash( char* Password, int length, char* HashStr, int StretchCount )
{
	char Buf[MAX_PASSWORD_LEN + 32];
	uint32_t hash[5];
	int i, j;
	unsigned char *p = (unsigned char *)HashStr;

	sha_memory( Password, length, hash );
	for(i = 0; i < StretchCount; i++)
	{
		memcpy(&Buf[0], &hash, 20);
		memcpy(&Buf[20], Password, length);
		sha_memory(Buf, 20 + length, hash);
	}

	for( i = 0; i < 5; i++ ){
		ulong rest = hash[i];
		for( j = 0; j < 8; j++ ){
			*p++ = (unsigned char)((rest & 0xf0000000) >> 28) + '@';
			rest <<= 4;
		}
	}
	*p = NUL;
}

void SetHashSalt( DWORD salt )
{
	// 全設定暗号化対応
//	unsigned char* pos = &SecretKey[strlen(SecretKey) + 1];
	unsigned char c[4];
	unsigned char* pos = &c[0];
	*pos++ = ( salt >> 24 ) & 0xff;
	*pos++ = ( salt >> 16 ) & 0xff;
	*pos++ = ( salt >>  8 ) & 0xff;
	*pos++ = ( salt       ) & 0xff;
	
//	SecretKeyLength = strlen( SecretKey ) + 5;
	SetHashSalt1(&c, 4);
}

// 全設定暗号化対応
void SetHashSalt1(void* Salt, int Length)
{
	void* p;
	if(Salt != NULL)
	{
		p = &SecretKey[strlen(SecretKey) + 1];
		memcpy(p, Salt, Length);
		SecretKeyLength = (int)strlen(SecretKey) + 1 + Length;
	}
	else
		SecretKeyLength = (int)strlen(SecretKey) + 1;
}

static void MaskSettingsData(std::string_view salt, void* Data, DWORD Size, bool EscapeZero) {
	BYTE mask[20];
	auto p = reinterpret_cast<BYTE*>(Data);
	for (DWORD i = 0; i < Size; i++) {
		if (i % 20 == 0) {
			BYTE buffer[FMAX_PATH * 2 + 1];
			for (DWORD nonce = i, j = 0; j < 16; j++)
				reinterpret_cast<DWORD*>(buffer)[j] = nonce = _byteswap_ulong(~nonce * 1566083941);
			memcpy(buffer + 64, data(salt), size(salt));
			memcpy(buffer + 64 + size(salt), SecretKey, SecretKeyLength);
			sha1(buffer, 64 + size_as<DWORD>(salt) + SecretKeyLength, buffer);
			for (int j = 0; j < 20; j++)
				buffer[j] ^= 0x36;
			for (int j = 20; j < 64; j++)
				buffer[j] = 0x36;
			sha1(buffer, 64, buffer + 64);
			for (int j = 0; j < 64; j++)
				buffer[j] ^= 0x6a;
			sha1(buffer, 84, mask);
		}
		if (!EscapeZero || p[i] != 0 && p[i] != mask[i % 20])
			p[i] ^= mask[i % 20];
	}
}

static void UnmaskSettingsData(std::string_view salt, void* Data, DWORD Size, bool EscapeZero) {
	MaskSettingsData(salt, Data, Size, EscapeZero);
}

// ポータブル版判定
int IsRegAvailable()
{
	int Sts;
	Config* h;
	Sts = NO;
	SetRegType(REGTYPE_REG);
	if(OpenReg("FFFTP", &h) == FFFTP_SUCCESS)
	{
		CloseReg(h);
		Sts = YES;
	}
	return Sts;
}

int IsIniAvailable()
{
	int Sts;
	Config* h;
	Sts = NO;
	SetRegType(REGTYPE_INI);
	if(OpenReg("FFFTP", &h) == FFFTP_SUCCESS)
	{
		CloseReg(h);
		Sts = YES;
	}
	return Sts;
}

// バージョン確認
int ReadSettingsVersion()
{
	Config *hKey3;
	int i;
	int Version;

	SetRegType(REGTYPE_INI);
	if((i = OpenReg("FFFTP", &hKey3)) != FFFTP_SUCCESS)
	{
		if(AskForceIni() == NO)
		{
			SetRegType(REGTYPE_REG);
			i = OpenReg("FFFTP", &hKey3);
		}
	}
	Version = INT_MAX;
	if(i == FFFTP_SUCCESS)
	{
		hKey3->ReadIntValueFromReg("Version", &Version);
		CloseReg(hKey3);
	}
	return Version;
}

// FileZilla XML形式エクスポート対応
void SaveSettingsToFileZillaXml()
{
	FILE* f;
	TIME_ZONE_INFORMATION tzi;
	int Level;
	int i;
	HOSTDATA Host;
	char Tmp[FMAX_PATH+1];
	char* p1;
	char* p2;
	if (auto const path = SelectFile(false, GetMainHwnd(), IDS_SAVE_SETTING, L"FileZilla.xml", L"xml", { FileType::Xml,FileType::All }); !std::empty(path))
	{
		if((f = _wfopen(path.c_str(), L"wt")) != NULL)
		{
			fputs("<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\" ?>\n", f);
			fputs("<FileZilla3>\n", f);
			fputs("<Servers>\n", f);
			GetTimeZoneInformation(&tzi);
			Level = 0;
			i = 0;
			while(CopyHostFromList(i, &Host) == FFFTP_SUCCESS)
			{
				while((Host.Level & SET_LEVEL_MASK) < Level)
				{
					fputs("</Folder>\n", f);
					Level--;
				}
				if(Host.Level & SET_LEVEL_GROUP)
				{
					fputs("<Folder expanded=\"1\">\n", f);
					fprintf(f, "%s&#x0A;\n", Host.HostName);
					Level++;
				}
				else
				{
					fputs("<Server>\n", f);
					fprintf(f, "<Host>%s</Host>\n", Host.HostAdrs);
					fprintf(f, "<Port>%d</Port>\n", Host.Port);
					if(Host.UseNoEncryption == YES)
						fprintf(f, "<Protocol>%s</Protocol>\n", "0");
					else if(Host.UseFTPES == YES)
						fprintf(f, "<Protocol>%s</Protocol>\n", "4");
					else if(Host.UseFTPIS == YES)
						fprintf(f, "<Protocol>%s</Protocol>\n", "3");
					else
						fprintf(f, "<Protocol>%s</Protocol>\n", "0");
					fprintf(f, "<Type>%s</Type>\n", "0");
					fprintf(f, "<User>%s</User>\n", Host.UserName);
					fprintf(f, "<Pass>%s</Pass>\n", Host.PassWord);
					fprintf(f, "<Account>%s</Account>\n", Host.Account);
					if(Host.Anonymous == YES || strlen(Host.UserName) == 0)
						fprintf(f, "<Logontype>%s</Logontype>\n", "0");
					else
						fprintf(f, "<Logontype>%s</Logontype>\n", "1");
					fprintf(f, "<TimezoneOffset>%d</TimezoneOffset>\n", tzi.Bias + Host.TimeZone * 60);
					if(Host.Pasv == YES)
						fprintf(f, "<PasvMode>%s</PasvMode>\n", "MODE_PASSIVE");
					else
						fprintf(f, "<PasvMode>%s</PasvMode>\n", "MODE_ACTIVE");
					fprintf(f, "<MaximumMultipleConnections>%d</MaximumMultipleConnections>\n", Host.MaxThreadCount);
					switch(Host.NameKanjiCode)
					{
					case KANJI_SJIS:
						fprintf(f, "<EncodingType>%s</EncodingType>\n", "Custom");
						fprintf(f, "<CustomEncoding>%s</CustomEncoding>\n", "Shift_JIS");
						break;
					case KANJI_JIS:
						// 非対応
						fprintf(f, "<EncodingType>%s</EncodingType>\n", "Auto");
						break;
					case KANJI_EUC:
						fprintf(f, "<EncodingType>%s</EncodingType>\n", "Custom");
						fprintf(f, "<CustomEncoding>%s</CustomEncoding>\n", "EUC-JP");
						break;
					case KANJI_SMB_HEX:
						// 非対応
						fprintf(f, "<EncodingType>%s</EncodingType>\n", "Auto");
						break;
					case KANJI_SMB_CAP:
						// 非対応
						fprintf(f, "<EncodingType>%s</EncodingType>\n", "Auto");
						break;
					case KANJI_UTF8N:
						fprintf(f, "<EncodingType>%s</EncodingType>\n", "UTF-8");
						break;
					case KANJI_UTF8HFSX:
						// 非対応
						fprintf(f, "<EncodingType>%s</EncodingType>\n", "Auto");
						break;
					default:
						fprintf(f, "<EncodingType>%s</EncodingType>\n", "Auto");
						break;
					}
					if(Host.FireWall == YES)
						fprintf(f, "<BypassProxy>%s</BypassProxy>\n", "0");
					else
						fprintf(f, "<BypassProxy>%s</BypassProxy>\n", "1");
					fprintf(f, "<Name>%s</Name>\n", Host.HostName);
					fprintf(f, "<LocalDir>%s</LocalDir>\n", Host.LocalInitDir);
					if(strchr(Host.RemoteInitDir, '\\') != NULL)
					{
						fputs("<RemoteDir>", f);
						fputs("8 0", f);
						strcpy(Tmp, Host.RemoteInitDir);
						p1 = Tmp;
						while(*p1 != '\0')
						{
							while(*p1 == '\\')
							{
								p1++;
							}
							if((p2 = strchr(p1, '\\')) != NULL)
							{
								*p2 = '\0';
								p2++;
							}
							else
								p2 = strchr(p1, '\0');
							if(*p1 != '\0')
								fprintf(f, " %zu %s", size(u8(p1)), p1);
							p1 = p2;
						}
						fputs("</RemoteDir>\n", f);
					}
					else if(strchr(Host.RemoteInitDir, '/') != NULL)
					{
						fputs("<RemoteDir>", f);
						fputs("1 0", f);
						strcpy(Tmp, Host.RemoteInitDir);
						p1 = Tmp;
						while(*p1 != '\0')
						{
							while(*p1 == '/')
							{
								p1++;
							}
							if((p2 = strchr(p1, '/')) != NULL)
							{
								*p2 = '\0';
								p2++;
							}
							else
								p2 = strchr(p1, '\0');
							if(*p1 != '\0')
								fprintf(f, " %zu %s", size(u8(p1)), p1);
							p1 = p2;
						}
						fputs("</RemoteDir>\n", f);
					}
					else
						fprintf(f, "<RemoteDir>%s</RemoteDir>\n", Host.RemoteInitDir);
					if(Host.SyncMove == YES)
						fprintf(f, "<SyncBrowsing>%s</SyncBrowsing>\n", "1");
					else
						fprintf(f, "<SyncBrowsing>%s</SyncBrowsing>\n", "0");
					fprintf(f, "%s&#x0A;\n", Host.HostName);
					fputs("</Server>\n", f);
				}
				i++;
			}
			while(Level > 0)
			{
				fputs("</Folder>\n", f);
				Level--;
			}
			fputs("</Servers>\n", f);
			// TODO: 環境設定
//			fputs("<Settings>\n", f);
//			fputs("</Settings>\n", f);
			fputs("</FileZilla3>\n", f);
			fclose(f);
		}
		else
			Message(IDS_FAIL_TO_EXPORT, MB_OK | MB_ICONERROR);
	}
}

// WinSCP INI形式エクスポート対応
void WriteWinSCPString(FILE* f, _In_z_ const char* s)
{
	const char* p;
	p = s;
	while(*p != '\0')
	{
		if(*p & 0x80)
		{
			p = NULL;
			break;
		}
		p++;
	}
	if(!p)
		fputs("%EF%BB%BF", f);
	while(*s != '\0')
	{
		switch(*s)
		{
		case '\t':
		case '\n':
		case '\r':
		case ' ':
		case '%':
		case '*':
		case '?':
		case '\\':
			fprintf(f, "%%%02X", *s & 0xff);
			break;
		default:
			if(*s & 0x80)
				fprintf(f, "%%%02X", *s & 0xff);
			else
				fputc(*s, f);
			break;
		}
		s++;
	}
}

void WriteWinSCPPassword(FILE* f, const char* UserName, const char* HostName, const char* Password)
{
	char Tmp[256];
	strcpy(Tmp, UserName);
	strcat(Tmp, HostName);
	strcat(Tmp, Password);
	fprintf(f, "%02X", ~(0xff ^ 0xa3) & 0xff);
	fprintf(f, "%02X", ~(0x00 ^ 0xa3) & 0xff);
	fprintf(f, "%02X", ~((unsigned char)strlen(Tmp) ^ 0xa3) & 0xff);
	fprintf(f, "%02X", ~(0x00 ^ 0xa3) & 0xff);
	Password = Tmp;
	while(*Password != '\0')
	{
		fprintf(f, "%02X", ~(*Password ^ 0xa3) & 0xff);
		Password++;
	}
}

void SaveSettingsToWinSCPIni()
{
	FILE* f;
	char HostPath[FMAX_PATH+1];
	int Level;
	int i;
	HOSTDATA Host;
	char Tmp[FMAX_PATH+1];
	char* p1;
	Message(IDS_NEED_EXSITING_WINSCP_INI, MB_OK);
	if (auto const path = SelectFile(false, GetMainHwnd(), IDS_SAVE_SETTING, L"WinSCP.ini", L"ini", { FileType::Ini, FileType::All }); !std::empty(path))
	{
		if((f = _wfopen(path.c_str(), L"at")) != NULL)
		{
			strcpy(HostPath, "");
			Level = 0;
			i = 0;
			while(CopyHostFromList(i, &Host) == FFFTP_SUCCESS)
			{
				while((Host.Level & SET_LEVEL_MASK) < Level)
				{
					if((p1 = strrchr(HostPath, '/')) != NULL)
						*p1 = '\0';
					if((p1 = strrchr(HostPath, '/')) != NULL)
						p1++;
					else
						p1 = HostPath;
					*p1 = '\0';
					Level--;
				}
				if(Host.Level & SET_LEVEL_GROUP)
				{
					strcat(HostPath, Host.HostName);
					strcat(HostPath, "/");
					Level++;
				}
				else
				{
					fputs("[Sessions\\", f);
					strcpy(Tmp, HostPath);
					strcat(Tmp, Host.HostName);
					WriteWinSCPString(f, Tmp);
					fputs("]\n", f);
					fputs("HostName=", f);
					WriteWinSCPString(f, Host.HostAdrs);
					fputs("\n", f);
					fprintf(f, "PortNumber=%d\n", Host.Port);
					fputs("UserName=", f);
					WriteWinSCPString(f, Host.UserName);
					fputs("\n", f);
					fprintf(f, "FSProtocol=%s\n", "5");
					fputs("LocalDirectory=", f);
					WriteWinSCPString(f, Host.LocalInitDir);
					fputs("\n", f);
					fputs("RemoteDirectory=", f);
					WriteWinSCPString(f, Host.RemoteInitDir);
					fputs("\n", f);
					if(Host.SyncMove == YES)
						fprintf(f, "SynchronizeBrowsing=%s\n", "1");
					else
						fprintf(f, "SynchronizeBrowsing=%s\n", "0");
					fputs("PostLoginCommands=", f);
					WriteWinSCPString(f, Host.InitCmd);
					fputs("\n", f);
					if(Host.FireWall == YES)
					{
						switch(FwallType)
						{
						case FWALL_NONE:
							break;
						case FWALL_FU_FP_SITE:
							break;
						case FWALL_FU_FP_USER:
							break;
						case FWALL_USER:
							break;
						case FWALL_OPEN:
							break;
						case FWALL_SOCKS4:
							fprintf(f, "ProxyMethod=%s\n", "1");
							break;
						case FWALL_SOCKS5_NOAUTH:
							break;
						case FWALL_SOCKS5_USER:
							fprintf(f, "ProxyMethod=%s\n", "2");
							break;
						case FWALL_FU_FP:
							break;
						case FWALL_SIDEWINDER:
							break;
						default:
							break;
						}
						fputs("ProxyHost=", f);
						WriteWinSCPString(f, FwallHost);
						fputs("\n", f);
						fprintf(f, "ProxyPort=%d\n", FwallPort);
						fputs("ProxyUsername=", f);
						WriteWinSCPString(f, FwallUser);
						fputs("\n", f);
					}
					switch(Host.NameKanjiCode)
					{
					case KANJI_SJIS:
						fprintf(f, "Utf=%s\n", "0");
						break;
					case KANJI_JIS:
						// 非対応
						break;
					case KANJI_EUC:
						// 非対応
						break;
					case KANJI_SMB_HEX:
						// 非対応
						break;
					case KANJI_SMB_CAP:
						// 非対応
						break;
					case KANJI_UTF8N:
						fprintf(f, "Utf=%s\n", "1");
						break;
					case KANJI_UTF8HFSX:
						// 非対応
						break;
					default:
						break;
					}
					if(Host.Pasv == YES)
						fprintf(f, "FtpPasvMode=%s\n", "1");
					else
						fprintf(f, "FtpPasvMode=%s\n", "0");
					if(Host.ListCmdOnly == YES && Host.UseMLSD == NO)
						fprintf(f, "FtpUseMlsd=%s\n", "0");
					fputs("FtpAccount=", f);
					WriteWinSCPString(f, Host.Account);
					fputs("\n", f);
					if(Host.NoopInterval > 0)
						fprintf(f, "FtpPingInterval=%d\n", Host.NoopInterval);
					else
						fprintf(f, "FtpPingType=%s\n", "0");
					if(Host.UseNoEncryption == YES)
						fprintf(f, "Ftps=%s\n", "0");
					else if(Host.UseFTPES == YES)
						fprintf(f, "Ftps=%s\n", "3");
					else if(Host.UseFTPIS == YES)
						fprintf(f, "Ftps=%s\n", "1");
					else
						fprintf(f, "Ftps=%s\n", "0");
					if(Host.FireWall == YES)
					{
						switch(FwallType)
						{
						case FWALL_NONE:
							break;
						case FWALL_FU_FP_SITE:
							fprintf(f, "FtpProxyLogonType=%s\n", "1");
							break;
						case FWALL_FU_FP_USER:
							fprintf(f, "FtpProxyLogonType=%s\n", "2");
							break;
						case FWALL_USER:
							fprintf(f, "FtpProxyLogonType=%s\n", "5");
							break;
						case FWALL_OPEN:
							fprintf(f, "FtpProxyLogonType=%s\n", "3");
							break;
						case FWALL_SOCKS4:
							break;
						case FWALL_SOCKS5_NOAUTH:
							break;
						case FWALL_SOCKS5_USER:
							break;
						case FWALL_FU_FP:
							break;
						case FWALL_SIDEWINDER:
							break;
						default:
							break;
						}
					}
					fputs("Password=", f);
					WriteWinSCPPassword(f, Host.UserName, Host.HostAdrs, Host.PassWord);
					fputs("\n", f);
					if(Host.FireWall == YES)
					{
						fputs("ProxyPasswordEnc=", f);
						WriteWinSCPPassword(f, FwallUser, FwallHost, FwallPass);
						fputs("\n", f);
					}
					fputs("\n", f);
				}
				i++;
			}
			fclose(f);
		}
		else
			Message(IDS_FAIL_TO_EXPORT, MB_OK | MB_ICONERROR);
	}
}
