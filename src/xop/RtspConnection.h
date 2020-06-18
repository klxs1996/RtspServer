// PHZ
// 2018-6-8

#ifndef _RTSP_CONNECTION_H
#define _RTSP_CONNECTION_H

#include "net/EventLoop.h"
#include "net/TcpConnection.h"
#include "RtpConnection.h"
#include "RtspMessage.h"
#include "DigestAuthentication.h"
#include "RTPPacketAnalysis.h"
#include "rtsp.h"
#include <iostream>
#include <functional>
#include <memory>
#include <vector>
#include <cstdint>

namespace xop
{

class RtspServer;
class MediaSession;

class RtspConnection : public TcpConnection
{
public:
	typedef std::function<void (SOCKET sockfd)> CloseCallback;
	typedef std::function<void(MediaSession* session)> NewSessionCallback;
	typedef std::function<void(std::string session_id)> RemoveSessionCallback;

	enum ConnectionMode
	{
		RTSP_FILE_SERVER, 
		RTSP_PUSH_SERVER,
		RTSP_PUSH_CLIENT,
		RTSP_SERVER
		//RTSP_CLIENT,
	};

	enum ConnectionState
	{
		START_CONNECT,
		START_PLAY,
		START_PUSH
	};

	RtspConnection() = delete;
	RtspConnection(std::shared_ptr<Rtsp> rtsp_server, TaskScheduler *task_scheduler, SOCKET sockfd);
	~RtspConnection();

	void SetNewSessionCallback(NewSessionCallback cb) { new_session_cb = cb; }
	void SetRemoveSessionCallback(RemoveSessionCallback cb) { remove_session_cb = cb; };

	MediaSessionId GetMediaSessionId()
	{ return session_id_; }

	TaskScheduler *GetTaskScheduler() const 
	{ return task_scheduler_; }

	void KeepAlive()
	{ alive_count_++; }

	bool IsAlive() const
	{
		if (IsClosed()) {
			return false;
		}

		if(rtp_conn_ != nullptr) {
			if (rtp_conn_->IsMulticast()) {
				return true;
			}			
		}

		return (alive_count_ > 0);
	}

	void ResetAliveCount()
	{ alive_count_ = 0; }

	int GetId() const
	{ return task_scheduler_->GetId(); }

	bool IsPlay() const
	{ return conn_state_ == START_PLAY; }

	bool IsRecord() const
	{ return conn_state_ == START_PUSH; }

private:
	friend class RtpConnection;
	friend class MediaSession;
	friend class RtspServer;
	friend class RtspPusher;

	bool OnReadyRead(BufferReader& buffer);
	void OnClose();
	void HandleRtcp(SOCKET sockfd);
	void HandleRtcp(BufferReader& buffer);   
	bool HandleTakeRequest(BufferReader& buffer);
	bool HandleTakeResponse(BufferReader& buffer);
	bool HandlePushRequest(BufferReader& buffer);
	bool HandlePushResponse(BufferReader& buffer);
	bool HandleRtspGetResponse(BufferReader& buffer);

	void SendRtspMessage(std::shared_ptr<char> buf, uint32_t size);
	int  ParseRtpPacket(const uint8_t* in, uint8_t* out, int len, bool& is_success,int& type);

	void HandleCmdOption();
	void HandleCmdDescribe();
	void HandleCmdSetup();
	void HandleCmdSetupPush();
	void HandleCmdPlay();
	void HandleCmdAnnounce();
	void HandleCmdTeardown();
	void HandleCmdRecord();
	void HandleCmdGetParamter();
	bool HandleAuthentication();
	void HandleRecord();

	void SetOptions();
	void SetMode(ConnectionMode mode = RTSP_FILE_SERVER) {
		conn_mode_ = mode;
	}
	void SendDescribe();
	void SendAnnounce();
	void SendSetup();
	void SendPlay();

	std::atomic_int alive_count_;
	std::weak_ptr<Rtsp> rtsp_;
	xop::TaskScheduler *task_scheduler_ = nullptr;

	ConnectionMode  conn_mode_ = RTSP_FILE_SERVER;
	ConnectionState conn_state_ = START_CONNECT;
	MediaSessionId  session_id_ = 0;
	NewSessionCallback new_session_cb = NULL;
	RemoveSessionCallback remove_session_cb = NULL;

	xop::AVFrame video_frame = { 0 };
	//uint8_t frame_sps[64] = { 0x00,0x00,0x00,0x01,0x67,0x64,0x00,0x28,0xAC,0x24,0x88,0x07,0x80,0x22,0x7E,0x58,0x40,0x00,0x00,0xFA,0x40,0x00,0x2E,0xE0,0x03,0xC6,0x0C,0xA8,0x00,0x00,0x00,0x01,0x68,0xEE,0x3C,0xB0 };
	uint8_t frame_sps[64] = { 0 };
	uint8_t frame_sps_size = 0;
	

	bool has_auth_ = true;
	bool is_complete = false;
	bool has_connect = false;
	std::string _nonce;
	std::unique_ptr<DigestAuthentication> auth_info_;

	std::shared_ptr<Channel>       rtp_channel_;
	std::shared_ptr<Channel>       rtcp_channels_[MAX_MEDIA_CHANNEL];
	std::unique_ptr<RtspRequest>   rtsp_request_;
	std::unique_ptr<RtspResponse>  rtsp_response_;
	std::shared_ptr<RtpConnection> rtp_conn_;
};

}

#endif
