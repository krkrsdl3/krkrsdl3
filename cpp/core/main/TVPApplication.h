
#ifndef __T_APPLICATION_H__
#define __T_APPLICATION_H__

#include "tjsVariant.h"
#include "tjsString.h"
#include "PlatformMutex.h"
#include <vector>
#include <functional>
#include <tuple>
#include <map>

void TVPCheckMemory();

// 見通しのよい方法に変更した方が良い

enum class eTVPActiveEvent
{
    onActive,
    onDeactive,
};

class tTVPApplication
{
    bool is_attach_console_;
    ttstr console_title_;
    //	AcceleratorKeyTable accel_key_;
    bool tarminate_;
    bool application_activating_;
    bool has_map_report_process_;

    class tTVPAsyncImageLoader* image_load_thread_;

private:
    void ShowException(const ttstr& e);

public:
    tTVPApplication();
    ~tTVPApplication();
    bool StartApplication();
    void Run();
    void ProcessMessages();

    bool IsAttachConsole() { return is_attach_console_; }

    bool IsTarminate() const { return tarminate_; }

    bool IsIconic()
    {
        return true; // そもそもウィンドウがない
    }

    void Terminate();
    void SetHintHidePause(int v) {}
    void SetShowHint(bool b) {}
    void SetShowMainForm(bool b) {}

    typedef std::function<void()> tMsg;

    void PostUserMessage(const std::function<void()>& func, void* param1 = nullptr, int param2 = 0);
    void FilterUserMessage(
        const std::function<void(std::vector<std::tuple<void*, int, tMsg>>&)>& func);

    void OnActivate();
    void OnDeactivate();
    void OnExit();
    void OnLowMemory();

    bool GetActivating() const { return application_activating_; }
    bool GetNotMinimizing() const;

    /**
     * 画像の非同期読込み要求
     */
    void LoadImageRequest(class iTJSDispatch2* owner, class tTJSNI_Bitmap* bmp, const ttstr& name);
    tTVPAsyncImageLoader* GetAsyncImageLoader() { return image_load_thread_; }

    void RegisterActiveEvent(
        void* host, const std::function<void(void*, eTVPActiveEvent)>& func /*empty = unregister*/);

private:
    tTJSCriticalSection m_msgQueueLock;

    std::vector<std::tuple<void*, int, tMsg>> m_lstUserMsg;
    std::map<void*, std::function<void(void*, eTVPActiveEvent)>> m_activeEvents;
};

extern class tTVPApplication* Application;

#endif // __T_APPLICATION_H__
