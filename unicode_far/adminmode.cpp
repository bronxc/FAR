/*
adminmode.cpp

Admin mode
*/
/*
Copyright (c) 2010 Far Group
All rights reserved.

Redistribution and use in source and binary forms, with or without
modification, are permitted provided that the following conditions
are met:
1. Redistributions of source code must retain the above copyright
   notice, this list of conditions and the following disclaimer.
2. Redistributions in binary form must reproduce the above copyright
   notice, this list of conditions and the following disclaimer in the
   documentation and/or other materials provided with the distribution.
3. The name of the authors may not be used to endorse or promote products
   derived from this software without specific prior written permission.

THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,
INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
(INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
*/

#include "headers.hpp"
#pragma hdrstop

#include "adminmode.hpp"
#include "config.hpp"
#include "lang.hpp"
#include "language.hpp"
#include "dialog.hpp"
#include "pathmix.hpp"
#include "colors.hpp"
#include "palette.hpp"
#include "lasterror.hpp"
#include "privilege.hpp"
#include "flink.hpp"


class AutoObject
{
public:
	AutoObject():
		Data(nullptr)
	{
	}

	LPVOID Allocate(size_t Size)
	{
		Free();
		Data=xf_malloc(Size);
		return Data;
	}

	void Free()
	{
		if(Data)
		{
			xf_free(Data);
			Data=nullptr;
		}
	}
	
	~AutoObject()
	{
		Free();
	}

	LPVOID Get()
	{
		return Data;
	}
	
	LPCWSTR GetStr()
	{
		return reinterpret_cast<wchar_t*>(Get());
	}

private:
	LPVOID Data;
};

bool RawReadPipe(HANDLE Pipe, LPVOID Data, DWORD DataSize)
{
	bool Result=false;
	DWORD n;
	if(ReadFile(Pipe, Data, DataSize, &n, nullptr) && n==DataSize)
	{
		Result=true;
	}
	return Result;
}

bool RawWritePipe(HANDLE Pipe, LPCVOID Data, DWORD DataSize)
{
	bool Result=false;
	DWORD n;
	if(WriteFile(Pipe, Data, DataSize, &n, nullptr) && n==DataSize)
	{
		Result=true;
	}
	return Result;
}

bool ReadPipeInt(HANDLE Pipe, int& Data)
{
	return RawReadPipe(Pipe, &Data, sizeof(Data));
}

bool WritePipeInt(HANDLE Pipe, int Data)
{
	return RawWritePipe(Pipe, &Data, sizeof(Data));
}

bool ReadPipeData(HANDLE Pipe, AutoObject& Data)
{
	bool Result=false;
	int DataSize=0;
	if(ReadPipeInt(Pipe, DataSize))
	{
		if(DataSize)
		{
			LPVOID Ptr=Data.Allocate(DataSize);
			if(Ptr)
			{
				if(RawReadPipe(Pipe, Ptr, DataSize))
				{
					Result=true;
				}
			}
		}
		else
		{
			Result=true;
		}
	}
	return Result;
}

bool WritePipeData(HANDLE Pipe, LPCVOID Data, int DataSize)
{
	bool Result=false;
	if(WritePipeInt(Pipe, DataSize))
	{
		if(!DataSize || RawWritePipe(Pipe, Data, DataSize))
		{
			Result=true;
		}
	}
	return Result;
}

AdminMode Admin;

AdminMode::AdminMode():
	Pipe(INVALID_HANDLE_VALUE),
	Approve(false),
	AskApprove(true)
{
}

AdminMode::~AdminMode()
{
	SendCommand(C_SERVICE_EXIT);
	DisconnectNamedPipe(Pipe);
	CloseHandle(Pipe);
}

bool AdminMode::ReadData(AutoObject& Data) const
{
	return ReadPipeData(Pipe, Data);
}

bool AdminMode::WriteData(LPCVOID Data,DWORD DataSize) const
{
	return WritePipeData(Pipe, Data, DataSize);
}

bool AdminMode::ReadInt(int& Data)
{
	return ReadPipeInt(Pipe, Data);
}

bool AdminMode::WriteInt(int Data)
{
	return WritePipeInt(Pipe, Data);
}

bool AdminMode::SendCommand(ADMIN_COMMAND Command)
{
	return WritePipeInt(Pipe, Command);
}

bool AdminMode::Initialize()
{
	bool Result=false;
	if(Pipe==INVALID_HANDLE_VALUE)
	{
		FormatString strPipe;
		strPipe << PIPE_NAME << GetCurrentProcessId();
		Pipe=CreateNamedPipe(strPipe, PIPE_ACCESS_DUPLEX, PIPE_TYPE_BYTE|PIPE_READMODE_BYTE|PIPE_WAIT, 1, 0, 0, 0, nullptr);
	}
	if(Pipe!=INVALID_HANDLE_VALUE)
	{
		if(SendCommand(C_SERVICE_TEST))
		{
			int SendData=rand();
			if(WritePipeInt(Pipe, SendData))
			{
				int RecvData=0;
				if(ReadPipeInt(Pipe, RecvData))
				{
					if((RecvData^Magic)==SendData)
					{
						Result=true;
					}
				}
			}
		}
		if(!Result)
		{
			FormatString strParam;
			strParam << L"/admin " << GetCurrentProcessId();
			SHELLEXECUTEINFO info=
			{
				sizeof(info),
				SEE_MASK_FLAG_NO_UI|SEE_MASK_UNICODE|SEE_MASK_NOASYNC,
				nullptr,
				L"runas",
				g_strFarModuleName,
				strParam,
			};
			if(ShellExecuteEx(&info))
			{
				DisconnectNamedPipe(Pipe);
				while(!ConnectNamedPipe(Pipe, nullptr))
				{
					Sleep(1);
				}
				Result=true;
			}
		}
	}
	return Result;
}

enum ADMINAPPROVEDLGITEM
{
	AAD_DOUBLEBOX,
	AAD_TEXT_SHIELD,
	AAD_TEXT_NEEDPERMISSION,
	AAD_TEXT_DETAILS,
	AAD_EDIT_OBJECT,
	AAD_CHECKBOX_REMEMBER,
	AAD_SEPARATOR,
	AAD_BUTTON_OK,
	AAD_BUTTON_SKIP,
};

LONG_PTR WINAPI AdminApproveDlgProc(HANDLE hDlg,int Msg,int Param1,LONG_PTR Param2)
{
	switch (Msg)
	{
	case DN_CTLCOLORDLGITEM:
		{
			if(Param1==AAD_EDIT_OBJECT)
			{
				int Color=FarColorToReal(COL_DIALOGTEXT);
				return ((Param2&0xFF00FF00)|(Color<<16)|Color);
			}
		}
		break;
	}
	return DefDlgProc(hDlg,Msg,Param1,Param2);
}

bool AdminMode::AdminApproveDlg(LPCWSTR Object)
{
	if(FrameManager && !FrameManager->ManagerIsDown() && AskApprove)
	{
		GuardLastError error;
		enum {DlgX=64,DlgY=11};
		DialogDataEx AdminApproveDlgData[]=
		{
			DI_DOUBLEBOX,3,1,DlgX-4,DlgY-2,0,0,0,0,MSG(MErrorAccessDenied),
			DI_TEXT,5,2,6,2,0,0,DIF_SETCOLOR|0xE9,0,L"\x2580\x2584",
			DI_TEXT,8,2,0,2,0,0,0,0,MSG(MAdminRequired1),
			DI_TEXT,8,3,0,3,0,0,0,0,MSG(MAdminRequired2),
			DI_EDIT,8,4,DlgX-6,4,0,0,DIF_READONLY|DIF_SETCOLOR|FarColorToReal(COL_DIALOGTEXT),0,Object,
			DI_CHECKBOX,5,6,0,6,0,1,0,0,MSG(MCopyRememberChoice),
			DI_TEXT,3,DlgY-4,0,DlgY-4,0,0,DIF_SEPARATOR,0,L"",
			DI_BUTTON,0,DlgY-3,0,DlgY-3,1,0,DIF_CENTERGROUP,1,MSG(MOk),
			DI_BUTTON,0,DlgY-3,0,DlgY-3,0,0,DIF_CENTERGROUP,0,MSG(MSkip),
		};
		MakeDialogItemsEx(AdminApproveDlgData,AdminApproveDlg);
		Dialog Dlg(AdminApproveDlg,countof(AdminApproveDlg),AdminApproveDlgProc);
		//Dlg.SetHelp(L"AdminApproveDlg");
		Dlg.SetPosition(-1,-1,DlgX,DlgY);
		Dlg.Process();
		Approve=(Dlg.GetExitCode()==AAD_BUTTON_OK);
		AskApprove=!AdminApproveDlg[AAD_CHECKBOX_REMEMBER].Selected;
	}
	return Approve;
}

bool AdminMode::CreateDirectory(LPCWSTR Object, LPSECURITY_ATTRIBUTES Attributes)
{
	bool Result=false;
	if(AdminApproveDlg(PointToName(Object)) && Initialize())
	{
		if(SendCommand(C_FUNCTION_CREATEDIRECTORY))
		{
			if(WriteData(Object,Object?(StrLength(Object)+1)*sizeof(WCHAR):0))
			{
				// BUGBUG: SecurityAttributes ignored
				int OpResult=0;
				if(ReadInt(OpResult) && OpResult)
				{
					Result=true;
				}
			}
		}
	}
	return Result;
}

bool AdminMode::RemoveDirectory(LPCWSTR Object)
{
	bool Result=false;
	if(AdminApproveDlg(PointToName(Object)) && Initialize())
	{
		if(SendCommand(C_FUNCTION_REMOVEDIRECTORY))
		{
			if(WriteData(Object,Object?(StrLength(Object)+1)*sizeof(WCHAR):0))
			{
				int OpResult=0;
				if(ReadInt(OpResult) && OpResult)
				{
					Result=true;
				}
			}
		}
	}
	return Result;
}

bool AdminMode::DeleteFile(LPCWSTR Object)
{
	bool Result=false;
	if(AdminApproveDlg(PointToName(Object)) && Initialize())
	{
		if(SendCommand(C_FUNCTION_DELETEFILE))
		{
			if(WriteData(Object,Object?(StrLength(Object)+1)*sizeof(WCHAR):0))
			{
				int OpResult=0;
				if(ReadInt(OpResult) && OpResult)
				{
					Result=true;
				}
			}
		}
	}
	return Result;
}

bool AdminMode::CopyFileEx(LPCWSTR From, LPCWSTR To, LPPROGRESS_ROUTINE ProgressRoutine, LPVOID Data, LPBOOL Cancel, DWORD Flags)
{
	bool Result=false;
	if(AdminApproveDlg(PointToName(From)) && Initialize())
	{
		if(SendCommand(C_FUNCTION_COPYFILEEX))
		{
			if(WriteData(From,From?(StrLength(From)+1)*sizeof(WCHAR):0))
			{
				// BUGBUG: ProgressRoutine, Data, Cancel ignored
				if(WriteData(To,To?(StrLength(To)+1)*sizeof(WCHAR):0))
				{
					if(WriteInt(Flags))
					{
						int OpResult=0;
						if(ReadInt(OpResult) && OpResult)
						{
							Result=true;
						}
					}
				}
			}
		}
	}
	return Result;
}

bool AdminMode::MoveFileEx(LPCWSTR From, LPCWSTR To, DWORD Flags)
{
	bool Result=false;
	if(AdminApproveDlg(PointToName(From)) && Initialize())
	{
		if(SendCommand(C_FUNCTION_MOVEFILEEX))
		{
			if(WriteData(From,From?(StrLength(From)+1)*sizeof(WCHAR):0))
			{
				if(WriteData(To,To?(StrLength(To)+1)*sizeof(WCHAR):0))
				{
					if(WriteInt(Flags))
					{
						int OpResult=0;
						if(ReadInt(OpResult) && OpResult)
						{
							Result=true;
						}
					}
				}
			}
		}
	}
	return Result;
}

bool AdminMode::SetFileAttributes(LPCWSTR Object, DWORD FileAttributes)
{
	bool Result=false;
	if(AdminApproveDlg(PointToName(Object)) && Initialize())
	{
		if(SendCommand(C_FUNCTION_SETFILEATTRIBUTES))
		{
			if(WriteData(Object,Object?(StrLength(Object)+1)*sizeof(WCHAR):0))
			{
				if(WriteInt(FileAttributes))
				{
					int OpResult=0;
					if(ReadInt(OpResult) && OpResult)
					{
						Result=true;
					}
				}
			}
		}
	}
	return Result;
}

bool AdminMode::CreateSymbolicLink(LPCWSTR Object, LPCWSTR Target, DWORD Flags)
{
	bool Result=false;
	if(AdminApproveDlg(PointToName(Object)) && Initialize())
	{
		if(SendCommand(C_FUNCTION_CREATESYMBOLICLINK))
		{
			if(WriteData(Object,Object?(StrLength(Object)+1)*sizeof(WCHAR):0))
			{
				if(WriteData(Target,Target?(StrLength(Target)+1)*sizeof(WCHAR):0))
				{
					if(WriteInt(Flags))
					{
						int OpResult=0;
						if(ReadInt(OpResult) && OpResult)
						{
							Result=true;
						}
					}
				}
			}
		}
	}
	return Result;
}

bool AdminMode::SetReparseDataBuffer(LPCWSTR Object,PREPARSE_DATA_BUFFER ReparseDataBuffer)
{
	bool Result=false;
	if(AdminApproveDlg(PointToName(Object)) && Initialize())
	{
		if(SendCommand(C_FUNCTION_SETREPARSEDATABUFFER))
		{
			if(WriteData(Object,Object?(StrLength(Object)+1)*sizeof(WCHAR):0))
			{
				if(WriteData(ReparseDataBuffer,ReparseDataBuffer?MAXIMUM_REPARSE_DATA_BUFFER_SIZE:0))
				{
					int OpResult=0;
					if(ReadInt(OpResult) && OpResult)
					{
						Result=true;
					}
				}
			}
		}
	}
	return Result;
}

bool IsUserAdmin()
{
	bool Result=false;
	SID_IDENTIFIER_AUTHORITY NtAuthority=SECURITY_NT_AUTHORITY;
	PSID AdministratorsGroup;
	if(AllocateAndInitializeSid(&NtAuthority,2,SECURITY_BUILTIN_DOMAIN_RID,DOMAIN_ALIAS_RID_ADMINS,0,0,0,0,0,0,&AdministratorsGroup))
	{
		BOOL IsMember=FALSE;
		if(CheckTokenMembership(nullptr,AdministratorsGroup,&IsMember)&&IsMember)
		{
			Result=true;
		}
		FreeSid(AdministratorsGroup);
	}
	return Result;
}

int AdminMain(int PID)
{
	int Result = ERROR_SUCCESS;

	SetErrorMode(SEM_FAILCRITICALERRORS|SEM_NOOPENFILEERRORBOX);

	Privilege
		BackupPrivilege(SE_BACKUP_NAME),
		RestorePrivilege(SE_RESTORE_NAME),
		CreateSymbolicLinkPrivilege(SE_CREATE_SYMBOLIC_LINK_NAME);

	FormatString strPipe;
	strPipe << PIPE_NAME << PID;
	HANDLE Pipe = CreateFile(strPipe,GENERIC_READ|GENERIC_WRITE, 0, nullptr, OPEN_EXISTING, 0, nullptr);
	if (Pipe != INVALID_HANDLE_VALUE)
	{
		bool Exit=false;
		while(!Exit)
		{
			Sleep(1);
			int Command;
			if(ReadPipeInt(Pipe, Command))
			{
				switch(Command)
				{
				case C_SERVICE_EXIT:
					{
						Exit=true;
					}
					break;

				case C_SERVICE_TEST:
					{
						int Test;
						if(ReadPipeInt(Pipe, Test))
						{
							Test^=Magic;
							WritePipeInt(Pipe, Test);
						}
					}
					break;

				case C_FUNCTION_CREATEDIRECTORY:
					{
						AutoObject Object;
						if(ReadPipeData(Pipe, Object))
						{
							// BUGBUG, SecurityAttributes ignored
							int OpResult=CreateDirectory(Object.GetStr(), NULL);
							WritePipeInt(Pipe, OpResult);
						}
					}
					break;

				case C_FUNCTION_REMOVEDIRECTORY:
					{
						AutoObject Object;
						if(ReadPipeData(Pipe, Object))
						{
							int OpResult=RemoveDirectory(Object.GetStr());
							WritePipeInt(Pipe, OpResult);
						}
					}
					break;

				case C_FUNCTION_DELETEFILE:
					{
						AutoObject Object;
						if(ReadPipeData(Pipe, Object))
						{
							int OpResult=DeleteFile(Object.GetStr());
							WritePipeInt(Pipe, OpResult);
						}
					}
					break;
				
				case C_FUNCTION_COPYFILEEX:
					{
						AutoObject From;
						if(ReadPipeData(Pipe, From))
						{
							AutoObject To;
							if(ReadPipeData(Pipe, To))
							{
								int Flags = 0;
								if(ReadPipeInt(Pipe, Flags))
								{
									// BUGBUG: ProgressRoutine, Data, Cancel ignored
									int OpResult=CopyFileEx(From.GetStr(), To.GetStr(), nullptr, nullptr, nullptr, Flags);
									WritePipeInt(Pipe, OpResult);
								}
							}
						}
					}
					break;

				case C_FUNCTION_MOVEFILEEX:
					{
						AutoObject From;
						if(ReadPipeData(Pipe, From))
						{
							AutoObject To;
							if(ReadPipeData(Pipe, To))
							{
								int Flags = 0;
								if(ReadPipeInt(Pipe, Flags))
								{
									int OpResult=MoveFileEx(From.GetStr(), To.GetStr(), Flags);
									WritePipeInt(Pipe, OpResult);
								}
							}
						}
					}
					break;

				case C_FUNCTION_SETFILEATTRIBUTES:
					{
						AutoObject Object;
						if(ReadPipeData(Pipe, Object))
						{
							int Attributes = 0;
							if(ReadPipeInt(Pipe, Attributes))
							{
								int OpResult=SetFileAttributes(Object.GetStr(), Attributes);
								WritePipeInt(Pipe, OpResult);
							}
						}
					}
					break;

				case C_FUNCTION_CREATESYMBOLICLINK:
					{
						AutoObject Object;
						if(ReadPipeData(Pipe, Object))
						{
							AutoObject Target;
							if(ReadPipeData(Pipe, Target))
							{
								int Flags = 0;
								if(ReadPipeInt(Pipe, Flags))
								{
									int OpResult=CreateSymbolicLinkInternal(Object.GetStr(), Target.GetStr(), Flags);
									WritePipeInt(Pipe, OpResult);
								}
							}
						}
					}
					break;

				case C_FUNCTION_SETREPARSEDATABUFFER:
					{
						AutoObject Object;
						if(ReadPipeData(Pipe, Object))
						{
							AutoObject ReparseDataBuffer;
							if(ReadPipeData(Pipe, ReparseDataBuffer))
							{
								int OpResult=SetREPARSE_DATA_BUFFER(Object.GetStr(), reinterpret_cast<PREPARSE_DATA_BUFFER>(ReparseDataBuffer.Get()));
								WritePipeInt(Pipe, OpResult);
							}
						}
					}
					break;

				}
			}
			else
			{
				if(GetLastError()==ERROR_BROKEN_PIPE)
				{
					Exit=true;
				}
			}
		}
	}
	return Result;
}