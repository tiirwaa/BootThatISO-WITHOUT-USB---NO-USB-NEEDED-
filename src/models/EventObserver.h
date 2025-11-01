#ifndef EVENTOBSERVER_H
#define EVENTOBSERVER_H

#include <string>

class EventObserver {
public:
    virtual ~EventObserver()                                                                         = default;
    virtual void onProgressUpdate(int progress)                                                      = 0;
    virtual void onLogUpdate(const std::string &message)                                             = 0;
    virtual void onButtonEnable()                                                                    = 0;
    virtual void onAskRestart()                                                                      = 0;
    virtual void onError(const std::string &message)                                                 = 0;
    virtual void onDetailedProgress(long long copied, long long total, const std::string &operation) = 0;
    virtual void onRecoverComplete(bool success)                                                     = 0;
};

#endif // EVENTOBSERVER_H
