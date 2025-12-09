#include "KeyCodeConv.h"
#include "tvpinputdefs.h"

#include "SDL3/SDL.h"

int TVPConvertMouseBtnToVKCode(tTVPMouseButton _mouseBtn)
{
	int btncode;
	switch (_mouseBtn) {
	case mbLeft:    btncode = VK_LBUTTON;	break;
	case mbMiddle:  btncode = VK_MBUTTON;	break;
	case mbRight:   btncode = VK_RBUTTON;	break;
	default:		btncode = 0;			break;
	}
	return btncode;
}

int TVPConvertKeyCodeToVKCode(int keyCode)
{
#define CASE(x) case SDL_SCANCODE_##x: return VK_##x

	SDL_Scancode tmp = (SDL_Scancode)keyCode;
	switch (tmp)
    {
            CASE(0);
            CASE(1);
            CASE(2);
            CASE(3);
            CASE(4);
            CASE(5);
            CASE(6);
            CASE(7);
            CASE(8);
            CASE(9);
            CASE(A);
            CASE(B);
            CASE(C);
            CASE(D);
            CASE(E);
            CASE(F);
            CASE(G);
            CASE(H);
            CASE(I);
            CASE(J);
            CASE(K);
            CASE(L);
            CASE(M);
            CASE(N);
            CASE(O);
            CASE(P);
            CASE(Q);
            CASE(R);
            CASE(S);
            CASE(T);
            CASE(U);
            CASE(V);
            CASE(W);
            CASE(X);
            CASE(Y);
            CASE(Z);
            CASE(F1);
            CASE(F2);
            CASE(F3);
            CASE(F4);
            CASE(F5);
            CASE(F6);
            CASE(F7);
            CASE(F8);
            CASE(F9);
            CASE(F10);
            CASE(F11);
            CASE(F12);
            CASE(PAUSE);
            CASE(ESCAPE);
            CASE(CANCEL);
            CASE(INSERT);
            CASE(HOME);
            CASE(DELETE);
            CASE(END);
            CASE(SPACE);
            case SDL_SCANCODE_PRINTSCREEN:
                return VK_PRINT;
                CASE(TAB);
                CASE(RETURN);
            case SDL_SCANCODE_SCROLLLOCK:
                return VK_SCROLL;
            case SDL_SCANCODE_SYSREQ:
                return VK_SNAPSHOT;
            case SDL_SCANCODE_BACKSPACE:
                return VK_BACK;
            case SDL_SCANCODE_CAPSLOCK:
                return VK_CAPITAL;
            case SDL_SCANCODE_LSHIFT:
                return VK_SHIFT; // LR the same
            case SDL_SCANCODE_RSHIFT:
                return VK_SHIFT;
            case SDL_SCANCODE_LCTRL:
                return VK_CONTROL; // LR the same
            case SDL_SCANCODE_RCTRL:
                return VK_CONTROL;
            case SDL_SCANCODE_LALT:
                return VK_MENU;
            case SDL_SCANCODE_RALT:
                return VK_MENU;
            case SDL_SCANCODE_MENU:
                return VK_APPS;
            case SDL_SCANCODE_PAGEUP:
                return VK_PRIOR;
            case SDL_SCANCODE_PAGEDOWN:
                return VK_NEXT;
            case SDL_SCANCODE_LEFT:
                return VK_LEFT;
            case SDL_SCANCODE_RIGHT:
                return VK_RIGHT;
            case SDL_SCANCODE_UP:
                return VK_UP;
            case SDL_SCANCODE_DOWN:
                return VK_DOWN;
            case SDL_SCANCODE_NUMLOCKCLEAR:
                return VK_NUMLOCK;
            case SDL_SCANCODE_KP_PLUS:
                return VK_ADD;
            case SDL_SCANCODE_KP_MINUS:
                return VK_SUBTRACT;
            case SDL_SCANCODE_KP_MULTIPLY:
                return VK_MULTIPLY;
            case SDL_SCANCODE_KP_DIVIDE:
                return VK_DIVIDE;
            case SDL_SCANCODE_KP_ENTER:
                return VK_RETURN;
            case SDL_SCANCODE_COMMA:
                return VK_OEM_COMMA;
            case SDL_SCANCODE_MINUS:
                return VK_OEM_MINUS;
            case SDL_SCANCODE_PERIOD:
                return VK_OEM_PERIOD;
            case SDL_SCANCODE_EQUALS:
                return VK_OEM_PLUS;
            case SDL_SCANCODE_SLASH:
                return VK_OEM_2;
            case SDL_SCANCODE_SEMICOLON:
                return VK_OEM_1;
            case SDL_SCANCODE_BACKSLASH:
                return VK_OEM_5;
            case SDL_SCANCODE_LEFTBRACKET:
                return VK_OEM_4;
            case SDL_SCANCODE_RIGHTBRACKET:
                return VK_OEM_6;
            case SDL_SCANCODE_MEDIA_PLAY:
                return VK_PLAY;
            default:
                return 0;
	}
}

std::string TVPGetVKCodeName(int keyCode)
{
	return "";
}
