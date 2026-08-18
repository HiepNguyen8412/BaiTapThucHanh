#include "Communication/SessionManager.h"
#include "Monitoring/Logger.h"

#include <sstream>

SessionManager::SessionManager(Logger& logger) : logger_(logger) {}
SessionManager::~SessionManager() { Stop(); }

// Bat cleanup thread de xoa cac session da disconnect qua thoi gian cho resume.
void SessionManager::Start()
{
    if (running_.exchange(true)) return;
    cleanupThread_ = std::thread(&SessionManager::CleanupLoop, this);
}

// Dung cleanup thread va giai phong cac SessionState con luu.
void SessionManager::Stop()
{
    if (!running_.exchange(false)) return;
    if (cleanupThread_.joinable()) cleanupThread_.join();
    std::lock_guard lock(mutex_);
    sessions_.clear();
}

// Tao SessionState moi va cap SessionId duy nhat sau HELLO thanh cong.
std::shared_ptr<SessionState> SessionManager::Create(
    const std::string& clientId,
    const ClientIdentity& identity)
{
    const std::uint64_t id = nextSessionId_++;
    auto state = std::make_shared<SessionState>(id, clientId, identity);
    {
        std::lock_guard lock(mutex_);
        sessions_[id] = state;
    }
    return state;
}

// Tim lai session cu theo SessionId va replay cac event co sequence > lastEventSeq.
std::shared_ptr<SessionState> SessionManager::Resume(
    std::uint64_t sessionId,
    const std::string& clientId,
    const ClientIdentity& identity,
    std::uint64_t lastEventSeq,
    AvProtocol::ServiceErrorCode& errorCode,
    std::wstring& message)
{
    std::shared_ptr<SessionState> state;
    {
        std::lock_guard lock(mutex_);
        const auto it = sessions_.find(sessionId);
        if (it == sessions_.end())
        {
            errorCode = AvProtocol::ServiceErrorCode::ResumeNotFound;
            message = L"Session was not found";
            return nullptr;
        }
        state = it->second;
    }

    if (state->IsExpired(std::chrono::steady_clock::now(), resumeWindow_))
    {
        std::lock_guard lock(mutex_);
        sessions_.erase(sessionId);
        errorCode = AvProtocol::ServiceErrorCode::ResumeExpired;
        message = L"Session resume window (10 seconds) expired";
        return nullptr;
    }
    if (state->ClientId() != clientId)
    {
        errorCode = AvProtocol::ServiceErrorCode::ResumeIdentityMismatch;
        message = L"RESUME clientId does not match original session";
        return nullptr;
    }
    if (!state->PrepareResume(identity, lastEventSeq, errorCode, message)) return nullptr;
    return state;
}

// Khong xoa session ngay; danh dau disconnect de client co cua so thoi gian reconnect/resume.
void SessionManager::MarkDisconnected(std::uint64_t sessionId)
{
    std::shared_ptr<SessionState> state;
    {
        std::lock_guard lock(mutex_);
        const auto it = sessions_.find(sessionId);
        if (it == sessions_.end()) return;
        state = it->second;
    }
    state->MarkDisconnected();
}

// Dinh ky loai cac session da disconnect qua TTL de tranh tang bo nho vo han.
void SessionManager::CleanupLoop()
{
    while (running_.load())
    {
        for (int i = 0; i < 10 && running_.load(); ++i)
        {
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }
        if (!running_.load()) break;

        const auto now = std::chrono::steady_clock::now();
        std::lock_guard lock(mutex_);
        for (auto it = sessions_.begin(); it != sessions_.end();)
        {
            if (it->second->IsExpired(now, resumeWindow_))
            {
                std::wstringstream stream;
                stream << L"Session expired: " << it->first;
                logger_.Info(stream.str());
                it = sessions_.erase(it);
            }
            else
            {
                ++it;
            }
        }
    }
}
