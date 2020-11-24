/*
*  Copyright (C) 2001 Jeff Doozan
*
*  This program is free software; you can redistribute it and/or modify
*  it under the terms of the GNU General Public License as published by
*  the Free Software Foundation; either version 2, or (at your option)
*  any later version.
*
*  This program is distributed in the hope that it will be useful,
*  but WITHOUT ANY WARRANTY; without even the implied warranty of
*  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
*  GNU General Public License for more details.
*
*  You should have received a copy of the GNU General Public License
*  along with this program; if not, write to the Free Software
*  Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
*/

#define _WIN32_WINNT 0x0500
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <commdlg.h>
#include <shlobj.h>
#include <shellapi.h>
#include <stdlib.h>
#include <string>
#include <list>
#include <malloc.h>

#define PLUGIN_NAME "Macro Extension Plugin"

#define KMELEON_PLUGIN_EXPORTS
#include "kmeleon_plugin.h"
#include "../../app/KMeleonConst.h"
#include "utils.h"
#include "strconv.h"
#include "md5.h"

#include "resrc1.h"
#include "object.h"

BOOL         APIENTRY DllMain(HANDLE hModule, DWORD  ul_reason_for_call, LPVOID lpReserved );
void         Create(HWND parent);
void         Config(HWND parent);

void         DoError(const char* msg, const char* filename, int line);
void         DoMenu(HMENU menu, char *param);
void         DoRebar(HWND rebarWnd);
int          DoAccel(const char *param);
long         DoMessage(const char *to, const char *from, const char *subject, long data1, long data2);
int          FindCommand(char *macroName);
int          GetConfigFiles(configFileType **configFiles);
int          GetCmds(kmeleonCommand* cmdList, long size);
int          Load();
MacroFile*   LoadMacros(const TCHAR *filename);
void         Close(HWND hWnd);
void         CloseFrame(HWND hWnd, LONG windows);
void         Quit();
void         SetOption(std::string option);
void         SetVarExp(int varid, std::string value);
std::string  strTrim(const std::string& instr);
std::string  ExecuteMacro(HWND hWnd, const char* name, const Context *parent = nullptr);
void         DisableMacros(MacroFile* mf);


#define WRONGARGS 0
#define WRONGTYPE 1

#ifdef _UNICODE
#define gUnicode TRUE
#else
BOOL gUnicode = FALSE;
#endif

kmeleonPlugin kPlugin = {
	KMEL_PLUGIN_VER_UTF8,
		PLUGIN_NAME,
		DoMessage
};

BOOL SetWindowTextUTF8(HWND hWnd, LPCSTR lpString)
{
	if (gUnicode)
		return SetWindowTextW(hWnd, CUTF8_to_UTF16(lpString));
	else
		return SetWindowTextA(hWnd, CUTF8_to_ANSI(lpString));
}

BOOL SetDlgItemTextUTF8(HWND hDlg, int nIDDlgItem, LPCSTR lpString)
{
	if (gUnicode)
		return SetDlgItemTextW(hDlg, nIDDlgItem, CUTF8_to_UTF16(lpString));
	else
		return SetDlgItemTextA(hDlg, nIDDlgItem, CUTF8_to_ANSI(lpString));
}

BOOL GetWindowTextUTF8(HWND hwnd, std::string & str)
{
	int len = GetWindowTextLength(hwnd) + 1;
	if (len<=1) return FALSE;

	if (gUnicode) {
		WCHAR* tmp = (WCHAR*)malloc(len * sizeof(WCHAR));
		if (!tmp) return FALSE;

		GetWindowTextW(hwnd, tmp, len);
		str.assign(CUTF16_to_UTF8(tmp));
		free(tmp);
	}
	else {
		char* tmp = (char*)malloc(len * sizeof(char));
		if (!tmp) return FALSE;

		GetWindowTextA(hwnd, tmp, len);
		str.assign(CANSI_to_UTF8(tmp));
		free(tmp);
	}
	return TRUE;
}

UINT GetDlgItemTextUTF8(HWND hwnd, int nIDDlgItem, std::string & str)
{
	return GetWindowTextUTF8(GetDlgItem(hwnd, nIDDlgItem),  str);
}

UINT GetDlgItemTextUTF8(HWND hwnd, int nIDDlgItem, char* lpString, int nMaxCount)
{
	char *res = 0;
	if (gUnicode)
	{
		WCHAR* tmp = (WCHAR*)malloc(nMaxCount * sizeof(WCHAR));
		GetDlgItemTextW(hwnd, IDC_ANSWER, tmp, nMaxCount);
		res = utf8_from_utf16(tmp);
		free(tmp);
	}
	else
	{
		char* tmp = (char*)malloc(nMaxCount * sizeof(char));
		GetDlgItemTextA(hwnd, IDC_ANSWER, tmp, nMaxCount);
		res = utf8_from_ansi(tmp);
		free(tmp);
	}
	if (res) {
		int len = strlen(res);	
		if (len>=nMaxCount) res[nMaxCount-1] = 0;
		strcpy(lpString, res);
		free(res);
		return len;
	}
	return 0;
}

int MessageBoxUTF8(HWND hWnd, const char* lpText, const char* lpCaption, UINT uType)
{
	if (gUnicode)
		return MessageBoxW(hWnd, CUTF8_to_UTF16(lpText), CUTF8_to_UTF16(lpCaption), uType);
	else
		return MessageBoxA(hWnd, CUTF8_to_ANSI(lpText), CUTF8_to_ANSI(lpCaption), uType);
}

kmeleonFunctions *kFuncs;

HINSTANCE ghInstance;
WINDOWPLACEMENT wpOld;

int wm_deferopenmsg = -1;
int iMacroCount = 0;
int iVarCount   = 0;
int bStartup    = 1;

std::string sGlobalArg;

Mac *M;

#include "parser.h"
#include "functions.h"

class Evaluator {
public:

	Context context;
	Mac* m;
	Statement* currentStat;

	Evaluator(Mac* mac, Context c)
	{
		m = mac;
		context = c;
		currentStat = NULL;
	}

	void EvalError(const char* msg)
	{
		DoError( msg, currentStat?currentStat->getFile():"", currentStat?currentStat->getLine():-1);
	}

	Value EvalCall(MacroNode *node)
	{
		assert(node->t == NODE_EXPR);
		assert(((Expression*)node)->et == EXPR_CALL);
		ExprCall* expr = static_cast<ExprCall*>(node);

		if (!expr->v) return 0;

		if (expr->v->t == VALUE_FUNCTION) {
			assert(expr->v->mf);

			FunctionData fd;
			fd.nparam = 0;
			fd.setContext(&context);
			fd.stat = currentStat;
			std::string args;

			Expression* param = static_cast<Expression*>(expr->firstParam);
			while (param) {
				param = static_cast<Expression*>(param->next);
				fd.nparam++;
			}

			fd.params = new Value[fd.nparam];
			param = static_cast<Expression*>(expr->firstParam);
			fd.nparam = 0;
			while (param) {
				fd.params[fd.nparam++] = EvalExpr(param);
				param = static_cast<Expression*>(param->next);
			}/*

			 Expression* param = static_cast<Expression*>(expr->child);
			 while (param) {
			 param = static_cast<Expression*>(param->next);
			 fd.nparam++;
			 }

			 fd.params = new Value[fd.nparam];
			 param = static_cast<Expression*>(expr->child);

			 fd.nparam = 0;
			 while (param) {
			 fd.params[fd.nparam++] = EvalExpr(param);
			 param = static_cast<Expression*>(param->next);
			 }*/

			Value v = (*(expr->v->mf))(&fd);
			delete [] fd.params;
			return v;
		}
		else if (expr->v->t == VALUE_MACRO) {
			if (expr->v->md)
				Evaluate(expr->v->md);
			else
				EvalError( "Call to an undefined macro.");
		}
		else if (expr->v->t == VALUE_UFUNCTION) {
			if (expr->v->uf) {

				// You don't want users to use the fact that currently
				// everything is static
				expr->v->uf->tds.emptyvar();
				Expression* param = static_cast<Expression*>(expr->firstParam);
				for (auto it = expr->v->uf->params.begin(); it != expr->v->uf->params.end(); it++) {
					if (!param) break;
					**it = EvalExpr(param);
					param = static_cast<Expression*>(param->next);
				}
				Value v;
				Evaluate(expr->v->uf, &v);
				return v;
			}
			else
				EvalError("Call to an undefined function.");
		}

		else
			EvalError("Invalid macro or function call.");
		return "";
	}

	Value EvalExpr(MacroNode *node)
	{
		if (!node) return Value();
		assert(ISEXPR(node));			
		Expr* expr = static_cast<Expr*>(node);

		switch (expr->et)
		{
			case EXPR_ADD: return EvalExpr(expr->A) + EvalExpr(expr->B); 
			case EXPR_SUB: return EvalExpr(expr->A) - EvalExpr(expr->B); 
			case EXPR_MUL: return EvalExpr(expr->A) * EvalExpr(expr->B); 
			case EXPR_DIV: return EvalExpr(expr->A) / EvalExpr(expr->B); 
			case EXPR_MOD: return EvalExpr(expr->A) % EvalExpr(expr->B); 
			case EXPR_GT: return EvalExpr(expr->A) > EvalExpr(expr->B); 
			case EXPR_LT: return EvalExpr(expr->A) < EvalExpr(expr->B); 
			case EXPR_GE: return EvalExpr(expr->A) >= EvalExpr(expr->B); 
			case EXPR_LE: return EvalExpr(expr->A) <= EvalExpr(expr->B); 
			case EXPR_NE: return EvalExpr(expr->A) != EvalExpr(expr->B); 
			case EXPR_EQ: return EvalExpr(expr->A) == EvalExpr(expr->B); 
			case EXPR_NOT: return !EvalExpr(expr->A);
			case EXPR_AND: return EvalExpr(expr->A) && EvalExpr(expr->B);
			case EXPR_OR: return EvalExpr(expr->A) || EvalExpr(expr->B);
			case EXPR_MINUS: return -EvalExpr(expr->A);
			case EXPR_CONCAT: return EvalExpr(expr->A).concat(EvalExpr(expr->B));
			case EXPR_COND: if (EvalExpr(expr->A).boolval()) return EvalExpr(expr->B); else return EvalExpr(expr->C); 
			case EXPR_ASSIGN: ((ExprValue*)expr->A)->Set(EvalExpr(expr->B)); return *(((ExprValue*)expr->A)->Get());
	
			case EXPR_CALL: return EvalCall(node); 
			case EXPR_VALUE: 
				if (!((ExprValue*)node)->Get()->isvalid())
					EvalError(("The variable '" + M->FindSymbol(((ExprValue*)node)->Get()) + "' is used without being initialized").c_str());
				return *(((ExprValue*)node)->Get()); 
			default: return 0;
		}
	}

	bool EvalStat(MacroNode* node, Value* v = nullptr) 
	{
		bool stop = false;
		assert(ISSTAT(node));
		Statement* stat = static_cast<Statement*>(node);
		currentStat = stat;
		switch (stat->st) {
			case STAT_EXPR: EvalExpr(stat->A); break;
			case STAT_RETURN: 
				if (!v) {
					EvalError("return outside a function");
					break;
				}
				*v = EvalExpr(stat->A); 
				stop = true;
				break;
			case STAT_WHILE: while (EvalExpr(stat->A).boolval() && !stop) if (stat->B) stop = Evaluate(stat->B, v); break;
			case STAT_IF: if (EvalExpr(stat->A).boolval()) {if (stat->B) stop = Evaluate(stat->B, v);} else if (stat->C) stop = Evaluate(stat->C, v);
		}
		currentStat = NULL;
		return stop;
	}


	bool Evaluate(MacroNode* node, Value* v = nullptr)
	{
		assert(ISLIST(node));
		StatList* snode = static_cast<StatList*>(node);
		node = snode->child;
		bool stop = false;
		while (node && !stop) {
			switch (node->t) {
				case NODE_STAT: 
					stop = EvalStat(node, v);
					break;
				case NODE_MACRO: 
					Value* v = m->FindSymbol(static_cast<MacroDef*>(node)->name);
					if (!v) 
						m->AddSymbol(static_cast<MacroDef*>(node)->name, static_cast<MacroDef*>(node));
					else
						v->md = static_cast<MacroDef*>(node);
			}
			node = node->next;
		}
		return stop;
	}
};


class ArgList {

	class Node {
	public:
		int id;
		std::string macro;
		std::string arg;
		class Node *next;
		Node(char *macro, char *arg) {
			this->macro = macro;
			this->arg = arg;
			id = kFuncs->GetCommandIDs(1);
			next = NULL;
		}
	};

protected:
	int min;
	int max;
	class Node *root;

public:
	ArgList() {
		root = NULL;
	}
	~ArgList() {
		class Node *ptr;
		while (root)
		{
			ptr = root;
			root = ptr->next;
			delete ptr;
		}

	}

	int add(char *macro, char *arg) {
		class Node *ptr = root;
		while (ptr && (strcmp(ptr->macro.c_str(), macro) || strcmp(ptr->arg.c_str(), arg)))
			ptr = ptr->next;
		if (ptr)
			return ptr->id;
		ptr = new Node(macro, arg);
		if (root == NULL)
			min = max = ptr->id;
		if (max < ptr->id)
			max = ptr->id;
		ptr->next = root;
		root = ptr;
		return ptr->id;
	}

	BOOL getfromid(int id, char**macro, char** arg) {
		class Node *ptr = root;
		while (ptr && ptr->id !=id) ptr = ptr->next;
		if (!ptr) return FALSE;
		*macro = (char*)ptr->macro.c_str();
		*arg = (char*)ptr->arg.c_str();
		return TRUE;
	}

	int getfromname(const char* macro, const char* arg) {
		class Node *ptr = root;
		if (!arg) arg = "";
		while (ptr && (strcmp(ptr->macro.c_str(), macro) || strcmp(ptr->arg.c_str(), arg)))
			ptr = ptr->next;
		if (ptr)
			return ptr->id;
		return 0;
	}

	bool execute(HWND hWnd, int id) {
		if (id < min || id > max)
			return false;
		class Node *ptr = root;
		while (ptr && ptr->id != id)
			ptr = ptr->next;
		if (!ptr || ptr->id!=id)
			return false;
		
		Value *v = M->FindSymbol("$ARG");
		assert(v);
		*v = Value(ptr->arg);
		ExecuteMacro(hWnd, ptr->macro.c_str(), false);
		*v = Value();
		return true;
	}
}* arglist;

int GetMacroState(int id)
{
	char *macroname, *param;
	if (!arglist->getfromid(id, &macroname, &param))
		return -1;	

	Value* macro = M->FindSymbol(macroname);
	if (!macro || !macro->ismacro() ||
		(!macro->md->btnChecked && ! macro->md->menuGrayed))
		 return -1;

	Value *v = M->FindSymbol("$ARG");
	assert(v);
	*v = Value(param);

	int res = 0;
	Context c = {NULL};
	if (macro->md->btnChecked) {
		Evaluator e(M, c);
		if (e.EvalExpr(macro->md->btnChecked).boolval())
			res |= 0x2;
	}

	if (macro->md->menuGrayed) {
		Evaluator e(M, c);
		if (e.EvalExpr(macro->md->menuGrayed).boolval())
			res |= 0x1;
	}

	return res;
}

const char* GetCmd(long id) {
	static MString str;
	char *macroname, *param;
	if (!arglist->getfromid(id, &macroname, &param))
		return nullptr;
		
	Value* macro = M->FindSymbol(macroname);
	if (!macro || !macro->ismacro() || !macro->md->macroInfo) 
		return nullptr;

	Context c = {0};
	Evaluator e(M, c);
	Value *v = M->FindSymbol("$ARG");
	*v = Value(param);
	str = e.EvalExpr(macro->md->macroInfo).strval();
	return str.c_str();
}

long DoMessage(const char *to, const char *from, const char *subject, long data1, long data2)
{  
	if (to[0] == '*' || stricmp(to, kPlugin.dllname) == 0) {
		if (strcmp(subject, "GetState") == 0) {
			unsigned res = GetMacroState(data1);
			if (res == -1) return 0;
			*((unsigned*)data2) = res;
			return 1;
		}
		else if (strcmp(subject, "Load") == 0) {
			Load();
		}
		else if (strcmp(subject, "Init") == 0) {
			ExecuteMacro(NULL, "OnInit", false);
		}
		else if (strcmp(subject, "Setup") == 0) {
			ExecuteMacro(NULL, "OnSetup", false);
		}
		else if (strcmp(subject, "UserSetup") == 0) {
			ExecuteMacro(NULL, "OnUserSetup", false);
		}
		else if (strcmp(subject, "Create") == 0) {
			Create((HWND)data1);
		}
		else if (strcmp(subject, "SwitchTab") == 0) {
			ExecuteMacro((HWND)data2, "OnSwitchTab", false);
		}
		else if (strcmp(subject, "CreateTab") == 0) {
			ExecuteMacro(data2?(HWND)data2:(HWND)data1, "OnOpenTab", false);
		}
		else if (strcmp(subject, "DestroyTab") == 0) {
			ExecuteMacro(data2?(HWND)data2:(HWND)data1, "OnCloseTab", false);
		}
		else if (strcmp(subject, "Close") == 0) {
			Close((HWND)data1);
		}
		else if (strcmp(subject, "Config") == 0) {
			Config((HWND)data1);
		}
		else if (strcmp(subject, "Quit") == 0) {
			Quit();
		}
		else if (strcmp(subject, "DoMenu") == 0) {
			DoMenu((HMENU)data1, (char *)data2);
		}
		else if (strcmp(subject, "DoRebar") == 0) {
			DoRebar((HWND)data1);
		}
		else if (strcmp(subject, "DoAccel") == 0) {
			*(int *)data2 = DoAccel((char *)data1);
		}
		else if (strcmp(subject, "GetConfigFiles") == 0) {
			*(int *)data2 = GetConfigFiles((configFileType**)data1);
		}
		else if (strcmp(subject, "GetCmds") == 0) {
			return GetCmds((kmeleonCommand*)data1, data2);
		}
		else if (strcmp(subject, "GetCmd") == 0) {
			data2 = (long)(void*)GetCmd((long)data1);
			return 1;
		}
		else if (strcmp(subject, "RunMacro") == 0) {
			Parser parser;
			int debug = 0;
			kPlugin.kFuncs->GetPreference(PREF_BOOL, "kmeleon.plugins.macros.debug", &debug, &debug);

			MacroNode* node = M->root.GetLast();
			if (!parser._init(M, strdup((char*)data1), debug))
				return 0;

			parser.parse();	
			
			Context c = {NULL};
			// Ugly hack to run new code
			MacroNode* child = M->root.child;
			M->root.child = node->next;
			Evaluator e(M, c);
			e.Evaluate(&M->root);
			M->root.child = child;
		}
		else return 0;

		return 1;
	}
	return 0;
}

void DisableMacros(MacroFile* mf)
{
	char prefload[MAX_PATH+40];
	strcpy(prefload, "kmeleon.plugins.macros.modules.");
	strcat(prefload, mf->name.c_str()); 
	strcat(prefload, ".load");

	int b = 0;			
	kFuncs->SetPreference(PREF_BOOL, prefload, (void*)&b, TRUE);
	mf->loaded = false;
}

Value ExecuteFunction(const char* name)
{
	if (!M) return "";
	std::string funcname = "!";
	funcname += name;
	Value* val = M->FindSymbol(funcname);
	if (!val || !val->isufunction())
		return "";

	ValueFunc* f = (ValueFunc*)val;
	f->uf->tds.emptyvar();
	/*Expression* param = static_cast<Expression*>(expr->firstParam);
	for (auto it = expr->v->uf->params.begin(); it != expr->v->uf->params.end(); it++) {
		if (!param) break;
		**it = EvalExpr(param);
		param = static_cast<Expression*>(param->next);
	}*/
	Value v, ret;
	Context context = {0};
	Evaluator eval(M, context);
	eval.Evaluate(f->uf, &ret);
	return ret;
}

std::string ExecuteMacro (HWND hWnd, const char* name, const Context* parent)
{
	if (!M) return "";

	Value* val = M->FindSymbol(name);
	if (!val || !val->ismacro())
		return "";

	ValueMacro* macro = (ValueMacro*)val;
	if (!macro->md || !macro->md->mf->loaded) {
		return "";
	}

	Context context;
	context.hWnd = hWnd;
	context.mf = macro->md->mf;
	context.origmf = parent ? (parent->origmf ? parent->origmf : parent->mf) : nullptr;
	Evaluator eval(M, context);
	eval.Evaluate(macro->md);
	return "";
}

std::list<MacroFile> *macroFileList;

bool LoadDir(const TCHAR* macrosDir)
{
	TCHAR szMacroFile[MAX_PATH];
	_tcscpy(szMacroFile, macrosDir);
	_tcscat(szMacroFile, _T("*.kmm"));

	WIN32_FIND_DATA FindFileData;
	HANDLE hFind;
	hFind = FindFirstFile(szMacroFile, &FindFileData);
	if (hFind == INVALID_HANDLE_VALUE)
		return false;

	do {
		if (_tcsicmp(_T("main.kmm"), FindFileData.cFileName) == 0)
			continue;

		char prefload[MAX_PATH+40];
		CharLowerBuff(FindFileData.cFileName, MAX_PATH);
		strcpy(prefload, "kmeleon.plugins.macros.modules.");
		strncat(prefload, CT_to_UTF8(FindFileData.cFileName), _tcsrchr(FindFileData.cFileName, '.')-FindFileData.cFileName); //uni
		strcat(prefload, ".load");

		BOOL load = TRUE;
		kFuncs->GetPreference(PREF_BOOL, prefload, &load, &load);

		if (load) {
			_tcscpy(szMacroFile, macrosDir);
			_tcscat(szMacroFile, FindFileData.cFileName);
			LoadMacros(szMacroFile);
		}
	} while (FindNextFile(hFind, &FindFileData));

	return true;
}

bool LoadNewMacros()
{
	char _macrosDir[MAX_PATH];
	kFuncs->GetFolder(RootFolder, _macrosDir, MAX_PATH);
	strcat(_macrosDir, "\\macros\\");

	char _profileMacrosDir[MAX_PATH];
	kFuncs->GetFolder(UserSettingsFolder, _profileMacrosDir, MAX_PATH);
	strcat(_profileMacrosDir, "\\macros\\");

	char szMacroFile[MAX_PATH];
	strcpy(szMacroFile, _macrosDir);
	strcat(szMacroFile, "main.kmm");
	if (!LoadMacros(CUTF8_to_T(szMacroFile)))
		return false;

	LoadDir(CUTF8_to_T(_macrosDir));
	LoadDir(CUTF8_to_T(_profileMacrosDir));

	return true;
}


BOOL APIENTRY DllMain(HANDLE hModule, DWORD  ul_reason_for_call, LPVOID lpReserved ) {
	switch (ul_reason_for_call) {
	  case DLL_PROCESS_ATTACH:
		  ghInstance = (HINSTANCE) hModule;
	  case DLL_THREAD_ATTACH:
	  case DLL_THREAD_DETACH:
	  case DLL_PROCESS_DETACH:
		  break;
	}
	return TRUE;
}

int Load() {
	kFuncs = kPlugin.kFuncs;
	arglist = new ArgList;
	macroFileList = new std::list<MacroFile>;
	M = new Mac();
	M->AddSymbol("$ARG", Value());
	InitFunctions(M);

#ifndef _UNICODE
	OSVERSIONINFO osinfo;
	osinfo.dwOSVersionInfoSize = sizeof(OSVERSIONINFO);
	GetVersionEx(&osinfo);
	gUnicode = (osinfo.dwPlatformId == VER_PLATFORM_WIN32_NT);
#endif

	char _szMacroFile[MAX_PATH];
	kFuncs->GetFolder(UserSettingsFolder, _szMacroFile, MAX_PATH);

	if (! *_szMacroFile)
		return 0;

	strcat(_szMacroFile, "\\macros.cfg");
	if (!LoadMacros(CUTF8_to_T(_szMacroFile))) {
		LoadNewMacros();
	}

	Context c = {NULL};
	Evaluator e(M, c);
	e.Evaluate(&M->root);

	wm_deferopenmsg = kPlugin.kFuncs->GetCommandIDs(1);

	return 1;
}

WNDPROC KMeleonWndProc;

LRESULT CALLBACK WndProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam);

void Create(HWND hWndParent) {
	KMeleonWndProc = (WNDPROC) GetWindowLong(hWndParent, GWL_WNDPROC);
	SetWindowLong(hWndParent, GWL_WNDPROC, (LONG)WndProc);

	PostMessage(hWndParent, WM_COMMAND, wm_deferopenmsg, 0);
}

void Config(HWND hWndParent) {
	char _cfgPath[MAX_PATH];
	kFuncs->GetFolder(UserSettingsFolder, _cfgPath, MAX_PATH);
	strcat(_cfgPath, "macros.cfg");
	ShellExecute(NULL, NULL, _T("notepad.exe"), CUTF8_to_T(_cfgPath), NULL, SW_SHOW);
}

configFileType g_configFiles[1];

int GetConfigFiles(configFileType **configFiles)
{
	char cfgPath[MAX_PATH];
	kFuncs->GetFolder(UserSettingsFolder, cfgPath, MAX_PATH);

	strcpy(g_configFiles[0].file, cfgPath);
	strcat(g_configFiles[0].file, "macros.cfg");

	strcpy(g_configFiles[0].label, "Macros");

	strcpy(g_configFiles[0].helpUrl, "http://www.kmeleon.org");

	*configFiles = g_configFiles;

	return 1;
}

int GetCmds(kmeleonCommand* cmdList, long size)
{
	int count = 0;
	TDS::iterator iter;   
	for( iter = M->tds.begin(); iter != M->tds.end(); iter++ ) {
		if (iter->second.ismacro() && iter->second.md && iter->second.md->macroInfo)
			count++;
	}
	if (!cmdList || !count) return count;

	count = 0;
	for( iter = M->tds.begin(); iter != M->tds.end(); iter++ ) {
		if (!iter->second.ismacro() || !iter->second.md || !iter->second.md->macroInfo)
			continue;

		Context c = {NULL};
		Evaluator e(M, c);
		MString str = e.EvalExpr(iter->second.md->macroInfo).strval();
		strncpy(cmdList->desc, CUTF8_to_ANSI(str.c_str()), sizeof(cmdList->desc));
		cmdList->desc[sizeof(cmdList->desc)-1] = 0;

		std::string name = std::string(kPlugin.dllname)+"("+iter->first+")";
		strncpy(cmdList->cmd, CUTF8_to_ANSI(name.c_str()), sizeof(cmdList->cmd));
		cmdList->cmd[sizeof(cmdList->cmd)-1] = 0;
		cmdList->id = arglist->getfromname(iter->first.c_str(), "");

		count++;
		cmdList++;
		if (--size == 0) break;
	}

	return count;
}

void Close(HWND hWnd) {
	ExecuteMacro(hWnd, "OnCloseWindow", false);
}

void CloseFrame(HWND hWnd, LONG windows) {
	ExecuteMacro(hWnd, "OnCloseGroup", false);
}

void Quit() {
	ExecuteMacro(NULL, "OnQuit", false);
	delete macroFileList;
	delete arglist;	
	delete M;
	M = nullptr;
}

void DoMenu(HMENU menu, char *param) {
	if (*param) {
		char *string = strchr(param, ',');
		if (string) {
			*string = 0;
			do {
				string++;
			} while (*string==' ' || *string=='\t');
		}

		char *arg = strchr(param, '(');
		if (arg) {
			*(arg++) = 0;
			char *p = strchr(arg, ')');
			if (p)
				*p = 0;
		}

		int id = arglist->getfromname(param, arg);
		if (id>0) {
			if (string)
				AppendMenu(menu, MF_STRING, id, CUTF8_to_T(string));
			else 
				AppendMenu(menu, MF_STRING, id, CUTF8_to_T(param));
		}
	}
}

int DoAccel(char const *aParam)
{
	char param[256] = {0};
	strncpy(param, aParam, 255);

	if (*param) {
		char *string = strchr(param, ',');
		if (string) {
			*string = 0;
			do {
				string++;
			} while (*string==' ' || *string=='\t');
		}

		char *arg = strchr(param, '(');
		if (arg) {
			*(arg++) = 0;
			char *p = strrchr(arg, ')');
			if (p) {
				*p = 0;
			}
		}

		int id = arglist->add(param, arg ? arg : "");
		return id;
	}
	return 0;
}

void DoRebar(HWND rebarWnd) {
}

MacroFile* LoadMacros(const TCHAR *filename)
{
	Parser parser;
	static int debug = -1;
	if (debug == -1) {
		int def = 0;
		kPlugin.kFuncs->GetPreference(PREF_BOOL, "kmeleon.plugins.macros.debug", &debug, &def);
	}

	MacroFile mf((TCHAR*)filename); // Meh
	auto iter = macroFileList->insert(macroFileList->begin(), mf);
	MacroFile* m = &*iter;

	if (!parser.init(M, *iter, debug))
		return nullptr;

	parser.parse();	
	m->loaded = true;

	char pref[MAX_PATH+40] = {0};

	sprintf_s(pref, "kmeleon.plugins.macros.modules.%s.denied", m->name.c_str());	
	kPlugin.kFuncs->GetPreference(PREF_BOOL, pref, &m->denied, &m->denied);
	if (m->denied) return m;

	sprintf_s(pref, "kmeleon.plugins.macros.modules.%s.trusted", m->name.c_str());
	int trust = 0;
	kPlugin.kFuncs->GetPreference(PREF_INT, pref, &trust, &trust);
	if (trust == 2) {
		m->denied = true;
	} else if (trust == 1) {
		char checksum[33];
		sprintf_s(pref, "kmeleon.plugins.macros.modules.%s.checksum", m->name.c_str());
		long len = kPlugin.kFuncs->GetPreference(PREF_STRING, pref, 0, 0);
		if (len == 32) {
			kPlugin.kFuncs->GetPreference(PREF_STRING, pref, checksum, checksum);

			MD5 md5;
			md5.digestFile(filename);
			if (strcmp(checksum, md5.digestChars) == 0)
				m->trusted = true;
		}
	}
	
	return m;
}

/************* misc *******************/


void DoError(const char* msg, const char* filename, int line)
{
	kPlugin.kFuncs->LogMessage("macro", msg, filename, line, errorFlag);
}

std::string strTrim(const std::string& instr)
{
	int lpos = 0;
	int rpos = instr.length()-1;
	while(lpos < rpos && ((instr.at(lpos) & 0x80) == 0) && isspace(instr.at(lpos))) ++lpos;
	while(rpos >= lpos && ((instr.at(lpos) & 0x80) == 0) && isspace(instr.at(rpos))) --rpos;
	return instr.substr(lpos,rpos-lpos+1);
}

// Subclassed window function

LRESULT CALLBACK WndProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam)
{
	switch (message) {

	case WM_COMMAND:
		if (LOWORD(wParam)==wm_deferopenmsg) {
			if (bStartup) {
				ExecuteMacro(hWnd, "OnStartup", false);
				bStartup = 0;
			}

			ExecuteMacro(hWnd, "OnOpenWindow", false);
		}
		else
			if (arglist->execute(kPlugin.kFuncs->GetCurrent(hWnd), LOWORD(wParam)))
				return 0;
		break;

	case UWM_UPDATEBUSYSTATE:
		if (wParam == 0)
			ExecuteMacro(IsWindow((HWND)lParam)?(HWND)lParam:hWnd, "OnLoad", false);
		break;

	case WM_ACTIVATE:
		if ( LOWORD(wParam) == WA_ACTIVE
			|| LOWORD(wParam) == WA_CLICKACTIVE) {	

			// Let k-meleon update the last browser frame value.
			LRESULT result = CallWindowProc(KMeleonWndProc, hWnd, message, wParam, lParam);
			ExecuteMacro(kPlugin.kFuncs->GetCurrent(hWnd), "OnActivateWindow", false);
			return result;
		}
		break;

	case WM_MENUSELECT: {
		UINT id = LOWORD(wParam);
		char *macroname, *param;
		if (!arglist->getfromid(id, &macroname, &param))
			break;
		
		Value* macro = M->FindSymbol(macroname);
		if (macro && macro->ismacro() && macro->md->macroInfo) {
			Context c = {hWnd};
			Evaluator e(M, c);
			kPlugin.kFuncs->SetStatusBarText(e.EvalExpr(macro->md->macroInfo).strval());
			return 0;
		}

		break;
	}

	case WM_INITMENUPOPUP: {

		// Let MFC do its little update
		LRESULT res = CallWindowProc(KMeleonWndProc, hWnd, message, wParam, lParam);

		HMENU menu = (HMENU)wParam;
		int count = GetMenuItemCount(menu);
		for (int i=0;i<count;i++)
		{
			int id = GetMenuItemID(menu, i);
			if (!id) continue;

			char *macroname, *param;
			if (!arglist->getfromid(id, &macroname, &param))
				continue;

			Value *v = M->FindSymbol("$ARG");
			assert(v);
			*v = Value(param);

			Value* macro = M->FindSymbol(macroname);
			if (macro && macro->ismacro()) {

				MENUITEMINFO mif;
				mif.cbSize = sizeof(mif);
				mif.fMask = MIIM_STATE;
				GetMenuItemInfo(menu, i, TRUE, &mif);

				Context c = {hWnd};

				//std::string label = EvalExpression(hWnd, macroList[mid]->menuString);
				if (macro->md->menuChecked) {
					Evaluator e(M, c);
					BOOL checked = e.EvalExpr(macro->md->menuChecked).boolval();
					mif.fState &= ~MF_CHECKED & ~MF_UNCHECKED;
					mif.fState |= checked ? MF_CHECKED : MF_UNCHECKED;
				}

				if (macro->md->menuGrayed) {
					Evaluator e(M, c);
					BOOL grayed = e.EvalExpr(macro->md->menuGrayed).boolval();
					mif.fState &= ~MF_GRAYED & ~MF_ENABLED;
					mif.fState |= grayed ? MF_GRAYED : MF_ENABLED;
				}

				::SetMenuItemInfo(menu, i, TRUE, &mif);
			}
			*v = Value();
		}
		return res;
	}
	}
	return CallWindowProc(KMeleonWndProc, hWnd, message, wParam, lParam);
}

// function mutilation protection
extern "C" {

	KMELEON_PLUGIN kmeleonPlugin *GetKmeleonPlugin() {
		return &kPlugin;
	}

}
