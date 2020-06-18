// PHZ
// 2018-6-10

#include "RtspConnection.h"
#include "RtspServer.h"
#include "MediaSession.h"
#include "MediaSource.h"
#include "net/SocketUtil.h"

#define USER_AGENT "-_-"
#define RTSP_DEBUG 0
#define MAX_RTSP_MESSAGE_SIZE 2048

using namespace xop;
using namespace std;

RtspConnection::RtspConnection(std::shared_ptr<Rtsp> rtsp, TaskScheduler *task_scheduler, SOCKET sockfd)
	: TcpConnection(task_scheduler, sockfd)
	, task_scheduler_(task_scheduler)
	, rtsp_(rtsp)
	, rtp_channel_(new Channel(sockfd))
	, rtsp_request_(new RtspRequest)
	, rtsp_response_(new RtspResponse)
{
	this->SetReadCallback([this](std::shared_ptr<TcpConnection> conn, xop::BufferReader& buffer) {
		return this->OnReadyRead(buffer);
	});

	this->SetCloseCallback([this](std::shared_ptr<TcpConnection> conn) {
		this->OnClose();
	});

	alive_count_ = 1;
	video_frame.buffer.reset(new uint8_t[250000]);

	rtp_channel_->SetReadCallback([this]() { this->HandleRead(); });
	rtp_channel_->SetWriteCallback([this]() { this->HandleWrite(); });
	rtp_channel_->SetCloseCallback([this]() { this->HandleClose(); });
	rtp_channel_->SetErrorCallback([this]() { this->HandleError(); });

	for(int chn=0; chn<MAX_MEDIA_CHANNEL; chn++) {
		rtcp_channels_[chn] = nullptr;
	}

	has_auth_ = true;
	if (rtsp->has_auth_info_) {
		has_auth_ = false;
		auth_info_.reset(new DigestAuthentication(rtsp->realm_, rtsp->username_, rtsp->password_));
	}	
}

RtspConnection::~RtspConnection()
{

}

bool RtspConnection::OnReadyRead(BufferReader& buffer)
{
	KeepAlive();

	if (buffer.ReadableBytes() <= 0) {
		return false; //close
	}

	if (conn_mode_ == RTSP_FILE_SERVER) {
		if (!HandleTakeRequest(buffer)){
			return false; 
		}
	}
	else if (conn_mode_ == RTSP_PUSH_SERVER)
	{
		if (!HandlePushRequest(buffer)) {
			return false;
		}
	}
	else if (conn_mode_ == RTSP_PUSH_CLIENT) {
		if (!HandleTakeResponse(buffer)) {           
			return false;
		}
	}
	else if (conn_mode_ == RTSP_SERVER)
	{
		if (!HandleRtspGetResponse(buffer)) {
			return false;
		}
	}

	if (buffer.ReadableBytes() > MAX_RTSP_MESSAGE_SIZE) {
		buffer.RetrieveAll();
	}

	return true;
}

void RtspConnection::OnClose()
{
	if(session_id_ != 0) {
		auto rtsp = rtsp_.lock();
		if (rtsp) {
			MediaSessionPtr media_session = rtsp->LookMediaSession(session_id_);
			if (media_session) {
				media_session->RemoveClient(this->GetSocket());
			}
		}	
	}

	for(int chn=0; chn<MAX_MEDIA_CHANNEL; chn++) {
		if(rtcp_channels_[chn] && !rtcp_channels_[chn]->IsNoneEvent()) {
			task_scheduler_->RemoveChannel(rtcp_channels_[chn]);
		}
	}
}

bool RtspConnection::HandleTakeRequest(BufferReader& buffer)
{
#if RTSP_DEBUG
	string str(buffer.Peek(), buffer.ReadableBytes());
	if (str.find("rtsp") != string::npos || str.find("RTSP") != string::npos)
	{
		std::cout << str << std::endl;
	}
#endif

	//shared_ptr<char> tmp_date(new char[buffer.ReadableBytes() + 1]);
	//memcpy(tmp_date.get(), buffer.Peek(), buffer.ReadableBytes());
	//this->SendRtspMessage(tmp_date, buffer.ReadableBytes());

    if (rtsp_request_->ParseRequest(&buffer)) {
		RtspRequest::Method method = rtsp_request_->GetMethod();
		if(method == RtspRequest::RTCP) {
			HandleRtcp(buffer);
			return true;
		}
		else if(!rtsp_request_->GotAll()) {
			return true;
		}
        
		switch (method)
		{
		case RtspRequest::OPTIONS:
			HandleCmdOption();
			break;
		case RtspRequest::DESCRIBE:
			HandleCmdDescribe();
			break;
		case RtspRequest::ANNOUNCE:
			HandleCmdAnnounce();
			break;
		case RtspRequest::SETUP:
			HandleCmdSetup();
			break;
		case RtspRequest::PLAY:
			HandleCmdPlay();
			break;
		case RtspRequest::TEARDOWN:
			HandleCmdTeardown();
			break;
		case RtspRequest::GET_PARAMETER:
			HandleCmdGetParamter();
			break;
		default:
			break;
		}

		if (rtsp_request_->GotAll()) {
			rtsp_request_->Reset();
		}
    }
	else {
		return false;
	}

	return true;
}

bool RtspConnection::HandleTakeResponse(BufferReader& buffer)
{
#if RTSP_DEBUG
	string str(buffer.Peek(), buffer.ReadableBytes());
	if(str.find("rtsp")!=string::npos || str.find("RTSP") != string::npos)
		cout << str << endl;
#endif

	if (rtsp_response_->ParseResponse(&buffer)) {
		RtspResponse::Method method = rtsp_response_->GetMethod();
		switch (method)
		{
		case RtspResponse::OPTIONS:
			SendAnnounce();
			break;
		case RtspResponse::ANNOUNCE:
		case RtspResponse::DESCRIBE:
			SendSetup();
			break;
		case RtspResponse::SETUP:
			SendSetup();
			break;
		case RtspResponse::RECORD:
			HandleRecord();
			break;
		default:            
			break;
		}
	}
	else {
		return false;
	}

	return true;
}

bool RtspConnection::HandlePushRequest(BufferReader& buffer)
{
#if RTSP_DEBUG
	string str(buffer.Peek(), buffer.ReadableBytes());
	if (str.find("rtsp") != string::npos || str.find("RTSP") != string::npos)
	{
		std::cout << str << std::endl;
	}
#endif

	//如果是负载数据
	if (buffer.IsPayload())
	{
		auto rtsp = rtsp_.lock();
		if (rtsp)
		{
			//	std::chrono::time_point<std::chrono::high_resolution_clock> begin = std::chrono::high_resolution_clock::now();
			bool is_success = true;

			static int frame_count = 0;
			if (++frame_count % 25 == 0 && frame_sps_size != 0)
			{
				static xop::AVFrame slice_head(64);
				slice_head.type = 0;
				slice_head.size = frame_sps_size;
				slice_head.timestamp = xop::H264Source::GetTimestamp();
				memcpy(slice_head.buffer.get(), frame_sps, frame_sps_size);
				rtsp->PushFrame(session_id_, xop::channel_0, slice_head);
				xop::Timer::Sleep(10);
			}

			int key_frame = 0;
			video_frame.type = 0;
			video_frame.timestamp = xop::H264Source::GetTimestamp();//放在解包前面是防止解析rtp包时耗费过大时间，从而导致时间戳不对应
			int res = ParseRtpPacket((uint8_t*)buffer.Peek(), video_frame.buffer.get(), buffer.ReadableBytes(), is_success, key_frame);

			if (is_success)
			{
				video_frame.size = res;
				rtsp->PushFrame(session_id_, MediaChannelId::channel_0, video_frame);
				buffer.RetrieveAll();
			}
		}
		return true;
	}

	printf("***************************read******************************\n");
	for (size_t i = 0; i < buffer.ReadableBytes(); i++)
	{
		printf("%c", buffer.Peek()[i]);
	}
	printf("\n");

	if (rtsp_request_->ParseRequest(&buffer)) {
		RtspRequest::Method method = rtsp_request_->GetMethod();
		if (method == RtspRequest::RTCP) {
			HandleRtcp(buffer);
			return true;
		}
		else if (!rtsp_request_->GotAll()) {
			return true;
		}

		switch (method)
		{
		case RtspRequest::OPTIONS:
			HandleCmdOption();
			break;
		case RtspRequest::DESCRIBE:
			HandleCmdDescribe();
			break;
		case RtspRequest::ANNOUNCE:
			HandleCmdAnnounce();
			break;
		case RtspRequest::SETUP:
			HandleCmdSetupPush();
			break;
		case RtspRequest::PLAY:
			HandleCmdPlay();
			break;
		case RtspRequest::TEARDOWN:
			HandleCmdTeardown();
			break;
		case RtspRequest::RECORD:
			HandleCmdRecord();
			break;
		default:
			break;
		}

		if (rtsp_request_->GotAll()) {
			rtsp_request_->Reset();
		}
	}
	else {
		return false;
	}

	return true;
}

bool RtspConnection::HandlePushResponse(BufferReader& buffer)
{
#if RTSP_DEBUG
	string str(buffer.Peek(), buffer.ReadableBytes());
	if (str.find("rtsp") != string::npos || str.find("RTSP") != string::npos)
		cout << str << endl;
#endif

	if (rtsp_response_->ParseResponse(&buffer)) {
		RtspResponse::Method method = rtsp_response_->GetMethod();
		switch (method)
		{
		case RtspResponse::OPTIONS:
			if (conn_mode_ == RTSP_PUSH_CLIENT) {
				SendAnnounce();
			}
			break;
		case RtspResponse::ANNOUNCE:
		case RtspResponse::DESCRIBE:
			SendSetup();
			break;
		case RtspResponse::SETUP:
			SendSetup();
			break;
		case RtspResponse::RECORD:
			HandleRecord();
			break;
		default:
			break;
		}
	}
	else {
		return false;
	}

	return true;
}

bool xop::RtspConnection::HandleRtspGetResponse(BufferReader & buffer)
{

	//printf("%d\n", buffer.ReadableBytes());

	if (buffer.IsPayload())
	{
		auto rtsp = rtsp_.lock();
		if (rtsp)
		{
			//	std::chrono::time_point<std::chrono::high_resolution_clock> begin = std::chrono::high_resolution_clock::now();
			bool is_success = true;

			static int frame_count = 0;
			if (++frame_count % 25 == 0 && frame_sps_size != 0)
			{
				static xop::AVFrame slice_head(64);
				slice_head.type = 0;
				slice_head.size = frame_sps_size;
				slice_head.timestamp = xop::H264Source::GetTimestamp();
				memcpy(slice_head.buffer.get(), frame_sps, frame_sps_size);
				rtsp->PushFrame(session_id_, xop::channel_0, slice_head);
				xop::Timer::Sleep(10);
			}

			int key_frame = 0;
			video_frame.type = 0;
			video_frame.timestamp = xop::H264Source::GetTimestamp();//放在解包前面是防止解析rtp包时耗费过大时间，从而导致时间戳不对应
			int res = ParseRtpPacket((uint8_t*)buffer.Peek(), video_frame.buffer.get(), buffer.ReadableBytes(), is_success, key_frame);

			if (is_success)
			{
				video_frame.size = res;
				rtsp->PushFrame(session_id_, MediaChannelId::channel_0, video_frame);
				buffer.RetrieveAll();
			}
		}
		return true;
	}

	printf("***************************read******************************\n");
	for (size_t i = 0; i < buffer.ReadableBytes(); i++)
	{
		printf("%c", buffer.Peek()[i]);
	}
	printf("\n");


	if (rtsp_response_->ParseResponse(&buffer)) {
		RtspResponse::Method method = rtsp_response_->GetMethod();
		switch (method)
		{
		case RtspResponse::OPTIONS:
			SendDescribe();
			break;
		case RtspResponse::ANNOUNCE:
		case RtspResponse::DESCRIBE:
			SendSetup();
			break;
		case RtspResponse::SETUP:
			SendPlay();
			break;
		case RtspResponse::RECORD:
			HandleRecord();
			break;
		default:
			break;
		}
	}
	else {
		return false;
	}

	buffer.RetrieveAll();
	return true;
}


void RtspConnection::SendRtspMessage(std::shared_ptr<char> buf, uint32_t size)
{
#if RTSP_DEBUG
	cout << buf.get() << endl;
#endif
	printf("***************************send******************************\n");
	for (size_t i = 0; i < size; i++)
	{
		printf("%c", buf.get()[i]);
	}

	this->Send(buf, size);
	return;
}

#if 1

int xop::RtspConnection::ParseRtpPacket(const uint8_t * in, uint8_t* out, int len, bool& is_success, int& type)
{
	while (in[1] == 0x01)
	{
		int new_size = ((in[2] & 0xFF) << 8) | (in[3] & 0xFF);

		in += new_size + 4;
		len -= new_size + 4;;
	}

	if (in[0] != 0x24 || in[1] != 0x00)
	{
		printf("warning111...\n");
		return 0;
	}

	int packet_size = 0;
	unsigned char nal_head[4] = { 0x00,0x00,0x00,0x01 };

	while (len > 0 && in[0] == 0x24 && in[1] == 0x00)
	{
		int new_size = ((in[2] & 0xFF) << 8) | (in[3] & 0xFF);

		printf("len:%d,size:%d\n", len,new_size);

		if (len - new_size < 4 || new_size > 1476)
			return 0;

		if (1472 > new_size)
		 {
			if (in[16] == 0x41 && in[17] == 0x9A)
			{
				memcpy(out, nal_head, 4);

				packet_size += 4;
				out += 4;

				in += 16;
				memcpy(out, in, new_size - 12);

				in += new_size - 12;
				out += new_size - 12;
				packet_size += new_size - 12;
				len -= new_size + 4;
			}
			else if ((in[17] & 0xE0) == 0x40)
			{
				in += 18;
				memcpy(out, in, new_size - 14);

				in += new_size - 14;
				out += new_size - 14;
				packet_size += new_size - 14;
				len -= new_size + 4;
			}
			else if (in[16] == 0x18 && in[17] == 0x00)//ffmpeg 推流出来的pps、sps解析帧类型为0
			{
				printf("......\n");
				uint8_t* tmp = frame_sps;
				memcpy(out, nal_head, 4);		//NAL head
				memcpy(tmp, nal_head, 4);
				out += 4;
				tmp += 4;
				packet_size += 4;
				frame_sps_size = 4;

				in += 17;
				int sps_size = ((in[0] & 0xFF) << 8) | (in[1] & 0xFF);

				in += 2;
				memcpy(out, in, sps_size);	//SPS
				memcpy(tmp, in, sps_size);

				in += sps_size;
				out += sps_size;
				tmp += sps_size;
				packet_size += sps_size;
				frame_sps_size += sps_size;

				memcpy(out, nal_head, 4);		//NAL head
				memcpy(tmp, nal_head, 4);
				out += 4;
				tmp += 4;
				packet_size += 4;
				frame_sps_size += 4;

				int pps_size = ((in[0] & 0xFF) << 8) | (in[1] & 0xFF);
				in += 2;
				memcpy(out, in, pps_size);				//PPS
				memcpy(tmp, in, pps_size);
				out += pps_size;
				in += pps_size;
				packet_size += pps_size;
				frame_sps_size += pps_size;

				int tmp_head_size = new_size - 19 - sps_size - 2 - pps_size;
				while (tmp_head_size != 0 && (in[0] != 0x24 || in[1] != 0x00))
				{
					memcpy(out, nal_head + 1, 3);		//NAL head  00 00 01
					out += 3;
					packet_size += 3;

					int size = (in[0] << 8) | (in[1]);
					in += 2;
					memcpy(out, in, size);
					out += size;
					in += size;
					packet_size += size;

				}

				len -= new_size + 4;
			}
			else if ((in[17] & 0x1F) > 0x00 && (in[17] & 0x1F) <= 0x0A)
			{
				unsigned char frame_type = (in[16] & 0xE0) | (in[17] & 0x1F);

				if (frame_type == 0x65)
					type = 1;

				memcpy(out, nal_head, 4);
				memcpy(out + 4, (void*)&frame_type, 1);

				out += 5;
				packet_size += 5;

				in += 18;
				memcpy(out, in, new_size - 14);

				in += new_size - 14;
				out += new_size - 14;
				packet_size += new_size - 14;
				len -= new_size + 4;
			}
			else
			{
				//for (size_t i = 0; i < new_size; i++)
				//{
				//	printf("%02X ", in[i]);
				//}
				//printf("\n");
				len -= new_size + 4;
				if (len <= 0)
					break;
				in += new_size + 4;	
			}

		}
		else if ((in[17] & 0x1F) < 12 && (in[17] & 0xE0) == 0x80)
		{
			memcpy(out, nal_head, 4);
			out[4] = (in[16] & 0xE0) | (in[17] & 0x1F);

			out += 5;
			packet_size += 5;

			in += 18;
			memcpy(out, in, new_size - 14);

			in += new_size - 14;
			out += new_size - 14;
			packet_size += new_size - 14;

			len -= new_size + 4;
		}
		else
		{
			in += 18;
			memcpy(out, in, new_size - 14);

			in += new_size - 14;
			out += new_size - 14;
			packet_size += new_size - 14;
			len -= new_size + 4;

		}
	}

	//	if(0 == len)
	{
		is_success = true;
		return packet_size;
	}
	return 0;
}
#else

int xop::RtspConnection::ParseRtpPacket(const uint8_t * in, uint8_t* out, int len, bool& is_success, int& type)
{
	uint8_t* in_data = (uint8_t*)in;
	uint8_t* out_data = out;
	uint8_t* tmp = in_data;
	int max_packet_size = MAX_RTP_PAYLOAD_SIZE + 12 + 4;
	int fua_head_size = 2;

	int copy_size = 0;

	if (len <= max_packet_size)//存在第一次接受就接收到不足1436字节但是属于FU_A分包的概率
	{
		copy_size = len - RTP_HEADER_SIZE - 4;
		memcpy(out_data, in_data + RTP_HEADER_SIZE + 4, copy_size);
		is_success = true;
	}
	else
	{
		while (len > max_packet_size)
		{
			in_data += RTP_HEADER_SIZE + RTP_TCP_HEAD_SIZE + fua_head_size;
			memcpy(out_data, in_data, MAX_RTP_PAYLOAD_SIZE - fua_head_size);
			len -= max_packet_size;
			in_data += MAX_RTP_PAYLOAD_SIZE - fua_head_size;
			out_data += MAX_RTP_PAYLOAD_SIZE - fua_head_size;
			copy_size += MAX_RTP_PAYLOAD_SIZE - fua_head_size;
		}
		
		if (len > 18 && (in_data[17] & 0x40) == 0x40)
		{
			is_success = true;

			in_data += RTP_HEADER_SIZE + RTP_TCP_HEAD_SIZE + fua_head_size;
			memcpy(out_data, in_data, len - RTP_HEADER_SIZE - RTP_TCP_HEAD_SIZE - fua_head_size);
			copy_size += len - RTP_HEADER_SIZE - RTP_TCP_HEAD_SIZE - fua_head_size;
		}
	}

	return copy_size;
}
#endif

void RtspConnection::HandleRtcp(BufferReader& buffer)
{    
	char *peek = buffer.Peek();
	if(peek[0] == '$' &&  buffer.ReadableBytes() > 4) {
		uint32_t pkt_size = peek[2]<<8 | peek[3];
		if(pkt_size +4 >=  buffer.ReadableBytes()) {
			buffer.Retrieve(pkt_size +4);  
		}
	}
}
 
void RtspConnection::HandleRtcp(SOCKET sockfd)
{
    char buf[1024] = {0};
    if(recv(sockfd, buf, 1024, 0) > 0) {
        KeepAlive();
    }
}

void RtspConnection::HandleCmdOption()
{
	std::shared_ptr<char> res(new char[2048]);
	int size = rtsp_request_->BuildOptionRes(res.get(), 2048);
	this->SendRtspMessage(res, size);
	is_complete = false;
}

void RtspConnection::HandleCmdDescribe()
{
	if (auth_info_ != nullptr && !HandleAuthentication()) {
		return;
	}

	if (rtp_conn_ == nullptr) {
		rtp_conn_.reset(new RtpConnection(shared_from_this()));
	}

	int size = 0;
	std::shared_ptr<char> res(new char[4096]);
	MediaSessionPtr media_session = nullptr;

	auto rtsp = rtsp_.lock();
	if (rtsp) {
		media_session = rtsp->LookMediaSession(rtsp_request_->GetRtspUrlSuffix());
	}
	
	if(!rtsp || !media_session) {
		size = rtsp_request_->BuildNotFoundRes(res.get(), 4096);
	}
	else {
		session_id_ = media_session->GetMediaSessionId();
		media_session->AddClient(this->GetSocket(), rtp_conn_);

		for(int chn=0; chn<MAX_MEDIA_CHANNEL; chn++) {
			MediaSource* source = media_session->GetMediaSource((MediaChannelId)chn);
			if(source != nullptr) {
				rtp_conn_->SetClockRate((MediaChannelId)chn, source->GetClockRate());
				rtp_conn_->SetPayloadType((MediaChannelId)chn, source->GetPayloadType());
			}
		}

		std::string sdp = media_session->GetSdpMessage(rtsp->GetVersion());
		if(sdp == "") {
			size = rtsp_request_->BuildServerErrorRes(res.get(), 4096);
		}
		else {
			size = rtsp_request_->BuildDescribeRes(res.get(), 4096, sdp.c_str());		
			has_connect = true;
			printf("has new connect!\n");
		}
	}

	SendRtspMessage(res, size);
	return ;
}

void RtspConnection::HandleCmdSetup()
{
	if (auth_info_ != nullptr && !HandleAuthentication()) {
		return;
	}

	int size = 0;
	std::shared_ptr<char> res(new char[4096]);
	MediaChannelId channel_id = rtsp_request_->GetChannelId();
	MediaSessionPtr media_session = nullptr;

	auto rtsp = rtsp_.lock();
	if (rtsp) {
		media_session = rtsp->LookMediaSession(session_id_);
	}

	if(!rtsp || !media_session)  {
		goto server_error;
	}

	if(media_session->IsMulticast())  {
		std::string multicast_ip = media_session->GetMulticastIp();
		if(rtsp_request_->GetTransportMode() == RTP_OVER_MULTICAST) {
			uint16_t port = media_session->GetMulticastPort(channel_id);
			uint16_t session_id = rtp_conn_->GetRtpSessionId();
			if (!rtp_conn_->SetupRtpOverMulticast(channel_id, multicast_ip.c_str(), port)) {
				goto server_error;
			}

			size = rtsp_request_->BuildSetupMulticastRes(res.get(), 4096, multicast_ip.c_str(), port, session_id);
		}
		else {
			goto transport_unsupport;
		}
	}
	else {
		if(rtsp_request_->GetTransportMode() == RTP_OVER_TCP) {
			uint16_t rtp_channel = rtsp_request_->GetRtpChannel();
			uint16_t rtcp_channel = rtsp_request_->GetRtcpChannel();
			uint16_t session_id = rtp_conn_->GetRtpSessionId();

			rtp_conn_->SetupRtpOverTcp(channel_id, rtp_channel, rtcp_channel);
			size = rtsp_request_->BuildSetupTcpRes(res.get(), 4096, rtp_channel, rtcp_channel, session_id);
		}
		else if(rtsp_request_->GetTransportMode() == RTP_OVER_UDP) {
			uint16_t cliRtpPort = rtsp_request_->GetRtpPort();
			uint16_t cliRtcpPort = rtsp_request_->GetRtcpPort();
			uint16_t session_id = rtp_conn_->GetRtpSessionId();

			if(rtp_conn_->SetupRtpOverUdp(channel_id, cliRtpPort, cliRtcpPort)) {                
				SOCKET rtcpfd = rtp_conn_->GetRtcpfd(channel_id);
				rtcp_channels_[channel_id].reset(new Channel(rtcpfd));
				rtcp_channels_[channel_id]->SetReadCallback([rtcpfd, this]() { this->HandleRtcp(rtcpfd); });
				rtcp_channels_[channel_id]->EnableReading();
				task_scheduler_->UpdateChannel(rtcp_channels_[channel_id]);
			}
			else {
				goto server_error;
			}

			uint16_t serRtpPort = rtp_conn_->GetRtpPort(channel_id);
			uint16_t serRtcpPort = rtp_conn_->GetRtcpPort(channel_id);
			size = rtsp_request_->BuildSetupUdpRes(res.get(), 4096, serRtpPort, serRtcpPort, session_id);
		}
		else {          
			goto transport_unsupport;
		}
	}

	SendRtspMessage(res, size);
	return ;

transport_unsupport:
	size = rtsp_request_->BuildUnsupportedRes(res.get(), 4096);
	SendRtspMessage(res, size);
	return ;

server_error:
	size = rtsp_request_->BuildServerErrorRes(res.get(), 4096);
	SendRtspMessage(res, size);
	return ;
}

void xop::RtspConnection::HandleCmdSetupPush()
{
	if (auth_info_ != nullptr && !HandleAuthentication()) {
		return;
	}

	int size = 0;
	std::shared_ptr<char> res(new char[4096]);
	MediaChannelId channel_id = rtsp_request_->GetChannelId();
	MediaSessionPtr media_session = nullptr;

	auto rtsp = rtsp_.lock();
	if (rtsp) {
		media_session = rtsp->LookMediaSession(session_id_);
	}

	if (!rtsp || !media_session) {
		goto server_error;
	}

	if (media_session->IsMulticast()) {
		std::string multicast_ip = media_session->GetMulticastIp();
		if (rtsp_request_->GetTransportMode() == RTP_OVER_MULTICAST) {
			uint16_t port = media_session->GetMulticastPort(channel_id);
			uint16_t session_id = rtp_conn_->GetRtpSessionId();
			if (!rtp_conn_->SetupRtpOverMulticast(channel_id, multicast_ip.c_str(), port)) {
				goto server_error;
			}

			size = rtsp_request_->BuildSetupMulticastRes(res.get(), 4096, multicast_ip.c_str(), port, session_id);
		}
		else {
			goto transport_unsupport;
		}
	}
	else {
		if (rtsp_request_->GetTransportMode() == RTP_OVER_TCP) {
			uint16_t rtp_channel = rtsp_request_->GetRtpChannel();
			uint16_t rtcp_channel = rtsp_request_->GetRtcpChannel();
			uint16_t session_id = rtp_conn_->GetRtpSessionId();

			rtp_conn_->SetupRtpOverTcp(channel_id, rtp_channel, rtcp_channel);
			size = rtsp_request_->BuildSetupTcpRes(res.get(), 4096, rtp_channel, rtcp_channel, session_id);
		}
		else if (rtsp_request_->GetTransportMode() == RTP_OVER_UDP) {
			uint16_t cliRtpPort = rtsp_request_->GetRtpPort();
			uint16_t cliRtcpPort = rtsp_request_->GetRtcpPort();
			uint16_t session_id = rtp_conn_->GetRtpSessionId();

			if (rtp_conn_->SetupRtpOverUdp(channel_id, cliRtpPort, cliRtcpPort)) {
				SOCKET rtcpfd = rtp_conn_->GetRtcpfd(channel_id);
				rtcp_channels_[channel_id].reset(new Channel(rtcpfd));
				rtcp_channels_[channel_id]->SetReadCallback([rtcpfd, this]() { this->HandleRtcp(rtcpfd); });
				rtcp_channels_[channel_id]->EnableReading();
				task_scheduler_->UpdateChannel(rtcp_channels_[channel_id]);
			}
			else {
				goto server_error;
			}

			uint16_t serRtpPort = rtp_conn_->GetRtpPort(channel_id);
			uint16_t serRtcpPort = rtp_conn_->GetRtcpPort(channel_id);
			size = rtsp_request_->BuildSetupUdpRes(res.get(), 4096, serRtpPort, serRtcpPort, session_id);
		}
		else {
			goto transport_unsupport;
		}
	}

	SendRtspMessage(res, size);
	return;

transport_unsupport:
	size = rtsp_request_->BuildUnsupportedRes(res.get(), 4096);
	SendRtspMessage(res, size);
	return;

server_error:
	size = rtsp_request_->BuildServerErrorRes(res.get(), 4096);
	SendRtspMessage(res, size);
	return;
}

void RtspConnection::HandleCmdPlay()
{
	if (auth_info_ != nullptr && !HandleAuthentication()) {
		return;
	}

	conn_state_ = START_PLAY;
	rtp_conn_->Play();

	uint16_t session_id = rtp_conn_->GetRtpSessionId();
	std::shared_ptr<char> res(new char[2048]);

	int size = rtsp_request_->BuildPlayRes(res.get(), 2048, nullptr, session_id);
	SendRtspMessage(res, size);
}

void xop::RtspConnection::HandleCmdAnnounce()
{
	int size = 0;
	auto rtsp = rtsp_.lock();
	std::shared_ptr<char> res(new char[4096]);

	//判断session是否已经存在
	if (rtsp)
	{
		MediaSessionPtr sesson = rtsp->LookMediaSession(rtsp_request_->GetRtspUrlSuffix());
		if (sesson)
		{
			size = rtsp_request_->BuildSessionExistRes(res.get(), 4096);
			SendRtspMessage(res, size);
			return;
		}
	}

	if (rtp_conn_ == nullptr) {
		rtp_conn_.reset(new RtpConnection(shared_from_this()));
	}

	MediaSession* session = MediaSession::CreateNew(rtsp_request_->GetRtspUrlSuffix());
	session->AddSource(channel_0, H264Source::CreateNew());
	if (new_session_cb)
	{
		new_session_cb(session);
	}

	session_id_ = session->GetMediaSessionId();

	session->AddClient(this->GetSocket(), rtp_conn_);

	for (int chn = 0; chn < MAX_MEDIA_CHANNEL; chn++) {
		MediaSource* source = session->GetMediaSource((MediaChannelId)chn);
		if (source != nullptr) {
			rtp_conn_->SetClockRate((MediaChannelId)chn, source->GetClockRate());
			rtp_conn_->SetPayloadType((MediaChannelId)chn, source->GetPayloadType());
		}
	}

	std::string sdp = session->GetSdpMessage(rtsp->GetVersion());
	if (sdp == "") {
		size = rtsp_request_->BuildServerErrorRes(res.get(), 4096);
	}
	else {
		size = rtsp_request_->BuildDescribeRes(res.get(), 4096, sdp.c_str());
	}


	SendRtspMessage(res, size);
}

void RtspConnection::HandleCmdTeardown()
{
	rtp_conn_->Teardown();

	if (remove_session_cb)
	{
		remove_session_cb(rtsp_request_->GetRtspUrlSuffix());//关闭session
	}

	uint16_t session_id = rtp_conn_->GetRtpSessionId();
	std::shared_ptr<char> res(new char[2048]);
	int size = rtsp_request_->BuildTeardownRes(res.get(), 2048, session_id);
	SendRtspMessage(res, size);
	is_complete = false;
	HandleClose();
}

void xop::RtspConnection::HandleCmdRecord()
{
	uint16_t session_id = rtp_conn_->GetRtpSessionId();
	std::shared_ptr<char> res(new char[2048]);
	int size = rtsp_request_->BuildRecordRes(res.get(), 2048, session_id);
	SendRtspMessage(res, size);
	is_complete = true;
}

void RtspConnection::HandleCmdGetParamter()
{
	uint16_t session_id = rtp_conn_->GetRtpSessionId();
	std::shared_ptr<char> res(new char[2048]);
	int size = rtsp_request_->BuildGetParamterRes(res.get(), 2048, session_id);
	SendRtspMessage(res, size);
}

bool RtspConnection::HandleAuthentication()
{
	if (auth_info_ != nullptr && !has_auth_) {
		std::string cmd = rtsp_request_->MethodToString[rtsp_request_->GetMethod()];
		std::string url = rtsp_request_->GetRtspUrl();

		if (_nonce.size() > 0 && (auth_info_->GetResponse(_nonce, cmd, url) == rtsp_request_->GetAuthResponse())) {
			_nonce.clear();
			has_auth_ = true;
		}
		else {
			std::shared_ptr<char> res(new char[4096]);
			_nonce = auth_info_->GetNonce();
			int size = rtsp_request_->BuildUnauthorizedRes(res.get(), 4096, auth_info_->GetRealm().c_str(), _nonce.c_str());
			SendRtspMessage(res, size);
			return false;
		}
	}

	return true;
}

void RtspConnection::SetOptions()
{
	if (rtp_conn_ == nullptr) {
		rtp_conn_.reset(new RtpConnection(shared_from_this()));
	}

	auto rtsp = rtsp_.lock();
	if (!rtsp) {
		HandleClose();
		return;
	}	

	rtsp_response_->SetUserAgent(USER_AGENT);
	rtsp_response_->SetRtspUrl(rtsp->GetRtspUrl().c_str());

	std::shared_ptr<char> req(new char[2048]);
	int size = rtsp_response_->BuildOptionReq(req.get(), 2048);
	SendRtspMessage(req, size);
}

void RtspConnection::SendAnnounce()
{
	MediaSessionPtr media_session = nullptr;

	auto rtsp = rtsp_.lock();
	if (rtsp) {
		media_session = rtsp->LookMediaSession(1);
	}

	if (!rtsp || !media_session) {
		HandleClose();
		return;
	}
	else {
		session_id_ = media_session->GetMediaSessionId();
		media_session->AddClient(this->GetSocket(), rtp_conn_);

		for (int chn = 0; chn<2; chn++) {
			MediaSource* source = media_session->GetMediaSource((MediaChannelId)chn);
			if (source != nullptr) {
				rtp_conn_->SetClockRate((MediaChannelId)chn, source->GetClockRate());
				rtp_conn_->SetPayloadType((MediaChannelId)chn, source->GetPayloadType());
			}
		}
	}

	std::string sdp = media_session->GetSdpMessage(rtsp->GetVersion());
	if (sdp == "") {
		HandleClose();
		return;
	}

	std::shared_ptr<char> req(new char[4096]);
	int size = rtsp_response_->BuildAnnounceReq(req.get(), 4096, sdp.c_str());
	SendRtspMessage(req, size);
}

void RtspConnection::SendDescribe()
{
	std::shared_ptr<char> req(new char[2048]);
	int size = rtsp_response_->BuildDescribeReq(req.get(), 2048);
	SendRtspMessage(req, size);
}

void RtspConnection::SendSetup()
{
	int size = 0;
	std::shared_ptr<char> buf(new char[2048]);	
	MediaSessionPtr media_session = nullptr;

	auto rtsp = rtsp_.lock();
	if (rtsp) {
		media_session = rtsp->LookMediaSession(session_id_);
		if (!media_session)
		{
			media_session.reset(MediaSession::CreateNew(rtsp_request_->GetRtspUrlSuffix()));
			media_session->AddSource(channel_0, H264Source::CreateNew());
			if (new_session_cb)
			{
				new_session_cb(media_session.get());
			}

			session_id_ = media_session->GetMediaSessionId();
			media_session->AddClient(this->GetSocket(), rtp_conn_);
		}
	}
	
	if (!rtsp || !media_session) {
		HandleClose();
		return;
	}

	if (media_session->GetMediaSource(channel_0) && !rtp_conn_->IsSetup(channel_0)) {
		rtp_conn_->SetupRtpOverTcp(channel_0, 0, 1);
		size = rtsp_response_->BuildSetupTcpReq(buf.get(), 2048, channel_0);
	}
	else if (media_session->GetMediaSource(channel_1) && !rtp_conn_->IsSetup(channel_1)) {
		rtp_conn_->SetupRtpOverTcp(channel_1, 2, 3);
		size = rtsp_response_->BuildSetupTcpReq(buf.get(), 2048, channel_1);
	}
	else {
		size = rtsp_response_->BuildRecordReq(buf.get(), 2048);
	}

	SendRtspMessage(buf, size);
}

void xop::RtspConnection::SendPlay()
{
	std::shared_ptr<char> buf(new char[2048]);
	int size = rtsp_response_->BuildPlay(buf.get(), 2048);
	SendRtspMessage(buf, size);
}


void RtspConnection::HandleRecord()
{
	conn_state_ = START_PUSH;
	rtp_conn_->Record();
}
