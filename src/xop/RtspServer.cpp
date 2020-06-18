#include "RtspServer.h"
#include "RtspConnection.h"
#include "net/SocketUtil.h"
#include "net/Logger.h"

using namespace xop;
using namespace std;

RtspServer::RtspServer(EventLoop* loop)
	: TcpServer(loop)
{

}

RtspServer::~RtspServer()
{
	
}

std::shared_ptr<RtspServer> RtspServer::Create(xop::EventLoop* loop)
{
	std::shared_ptr<RtspServer> server(new RtspServer(loop));
	return server;
}

MediaSessionId RtspServer::AddSession(MediaSession* session)
{
    std::lock_guard<std::mutex> locker(mutex_);

    if (rtsp_suffix_map_.find(session->GetRtspUrlSuffix()) != rtsp_suffix_map_.end()) {
        return 0;
    }

    std::shared_ptr<MediaSession> media_session(session); 
    MediaSessionId sessionId = media_session->GetMediaSessionId();
	rtsp_suffix_map_.emplace(std::move(media_session->GetRtspUrlSuffix()), sessionId);
	media_sessions_.emplace(sessionId, std::move(media_session));

    return sessionId;
}

void RtspServer::RemoveSession(MediaSessionId sessionId)
{
    std::lock_guard<std::mutex> locker(mutex_);

    auto iter = media_sessions_.find(sessionId);
    if(iter != media_sessions_.end()) {
        rtsp_suffix_map_.erase(iter->second->GetRtspUrlSuffix());
        media_sessions_.erase(sessionId);
    }
}

bool xop::RtspServer::OpenUrl(std::string url)
{
	Rtsp::parseRtspUrl(url);

	TcpSocket socket;
	
	for (size_t i = 0; i < MAX_GET_STREAM_SOCKET; i++)
	{
		if (_rtsp_socket[i] == 0)
		{
			_rtsp_socket[i] = socket.Create();
			break;
		}
		if (i == MAX_GET_STREAM_SOCKET - 1)
		{
			//超出最大连接限制
			return false;
		}
	}

	if (!socket.Connect(rtsp_url_info_.ip, rtsp_url_info_.port, 10000))
	{
		socket.Close();
	}

	std::shared_ptr<xop::RtspConnection> _rtsp_connect(new xop::RtspConnection(shared_from_this(), event_loop_->GetTaskScheduler().get(), socket.GetSocket()));
	event_loop_->AddTriggerEvent([_rtsp_connect]() {
		_rtsp_connect->SetOptions();
		_rtsp_connect->SetMode(RtspConnection::RTSP_SERVER);
	});

	rtsp_get_socket_list.push_back(_rtsp_connect);

	return true;
}

MediaSessionPtr RtspServer::LookMediaSession(const std::string& suffix)
{
    std::lock_guard<std::mutex> locker(mutex_);

    auto iter = rtsp_suffix_map_.find(suffix);
    if(iter != rtsp_suffix_map_.end()) {
        MediaSessionId id = iter->second;
        return media_sessions_[id];
    }

    return nullptr;
}

MediaSessionPtr RtspServer::LookMediaSession(MediaSessionId sessionId)
{
    std::lock_guard<std::mutex> locker(mutex_);

    auto iter = media_sessions_.find(sessionId);
    if(iter != media_sessions_.end()) {
        return iter->second;
    }

    return nullptr;
}

bool RtspServer::PushFrame(MediaSessionId sessionId, MediaChannelId channelId, AVFrame& frame)
{
    std::shared_ptr<MediaSession> sessionPtr = nullptr;

    {
        std::lock_guard<std::mutex> locker(mutex_);
        auto iter = media_sessions_.find(sessionId);
        if (iter != media_sessions_.end()) {
            sessionPtr = iter->second;
        }
        else {
            return false;
        }
    }

	if (sessionPtr != nullptr && sessionPtr->GetNumClient() != 0) {
		return sessionPtr->HandleFrame(channelId, frame);
	}

    return false;
}

TcpConnection::Ptr RtspServer::OnConnect(SOCKET sockfd)
{
	auto conn = std::make_shared<RtspConnection>(shared_from_this(), event_loop_->GetTaskScheduler().get(), sockfd);

	if (PUSH_SERVER == _type || RTSP_SERVER == _type)
	{
		conn->SetMode(RtspConnection::RTSP_PUSH_SERVER);
		conn->SetNewSessionCallback([=](MediaSession* session) {
			if (session){
				MediaSessionId id = this->AddSession(session);
			}
		});
		conn->SetRemoveSessionCallback([=](std::string suffix)
		{
			if (!suffix.empty()){
				auto iter = rtsp_suffix_map_.find(suffix);
				if (iter != rtsp_suffix_map_.end()){
					RemoveSession(iter->second);
				}
			}
		});

	}
	else
	{
		conn->SetMode(RtspConnection::RTSP_FILE_SERVER);

	}
	has_connect = true;

	return conn;
}

