//#include <thread>
//#include <memory>
//#include <iostream>
//#include <string>
//#include "../example/rtsp_h264_file.h"
//
//void SendFrameThread(xop::RtspServer* rtsp_server, xop::MediaSessionId session_id, H264File* h264_file);
//
//bool new_connect = false;
//
//int main(int argc, char **argv)
//{
//	if (argc != 2) {
//		printf("Usage: %s test.h264\n", argv[0]);
//		return 0;
//	}
//
//	H264File h264_file;
//	if (!h264_file.Open("D:/test/test.h264")) {
//		printf("Open %s failed.\n", argv[1]);
//		return 0;
//	}
//
//	std::string suffix = "live";
//	std::string ip = "127.0.0.1";
//	std::string port = "665";
//	std::string rtsp_url = "rtsp://" + ip + ":" + port + "/" + suffix;
//
//	std::shared_ptr<xop::EventLoop> event_loop(new xop::EventLoop());
//	std::shared_ptr<xop::RtspServer> server = xop::RtspServer::Create(event_loop.get());
//	server->SetServerType(xop::RtspServer::FILE_SERVER);
//
//	if (!server->Start("0.0.0.0", atoi(port.c_str()))) {
//		printf("RTSP Server listen on %s failed.\n", port.c_str());
//		return 0;
//	}
//
//#ifdef AUTH_CONFIG
//	server->SetAuthConfig("-_-", "admin", "12345");
//#endif
//
//	xop::MediaSession *session = xop::MediaSession::CreateNew("live");
//	session->AddSource(xop::channel_0, xop::H264Source::CreateNew());
//	//session->StartMulticast(); 
//	session->SetNotifyCallback([](xop::MediaSessionId session_id, uint32_t clients) {
//		new_connect = true;
//		std::cout << "The number of rtsp clients: " << clients << std::endl;
//	});
//
//	xop::MediaSessionId session_id = server->AddSession(session);
//
//	std::thread t1(SendFrameThread, server.get(), session_id, &h264_file);
//	t1.detach();
//
//	std::cout << "Play URL: " << rtsp_url << std::endl;
//
//	while (1) {
//		xop::Timer::Sleep(100);
//	}
//
//	getchar();
//	return 0;
//} 
//
//void SendFrameThread(xop::RtspServer* rtsp_server, xop::MediaSessionId session_id, H264File* h264_file)
//{
//	int buf_size = 2000000;
//	std::unique_ptr<uint8_t> frame_buf(new uint8_t[buf_size]);
//	std::shared_ptr<char> file_head(new char[256]);
//	
//	int res = h264_file->ReadPPSAndSPS(file_head.get(), 256);
//	int frame_type = 0;	//if frame is I frame,set value 1 
//	int frame_count = 0;
//
//	
//	while (1) {
//		bool end_of_frame = false;
//		int frame_size = h264_file->ReadFrame((char*)frame_buf.get(), buf_size, &end_of_frame, frame_type);
//
//		//printf("frame_size:%d\n", frame_size);
//
//		char buf[64] = { 0 };
//		sprintf(buf, "%d\n", frame_size);
//		static FILE* fp = fopen("D:/test/test2", "wb");
//		if (fp)
//		{
//			fwrite(buf, strlen(buf), 1, fp);
//		}
//
//		if (++frame_count % 25 == 0 && res != 0)
//		{
//			printf("size:%d\n", res);
//			xop::AVFrame headFrame = { 0 };
//			headFrame.type = 0;
//			headFrame.size = res;
//			headFrame.timestamp = xop::H264Source::GetTimestamp();
//			headFrame.buffer.reset(new uint8_t[res]);
//			memcpy(headFrame.buffer.get(), file_head.get(), res);
//			rtsp_server->PushFrame(session_id, xop::channel_0, headFrame);
//			xop::Timer::Sleep(20);
//		}
//
//		if (frame_size > 0) {
//			xop::AVFrame videoFrame = { 0 };
//			videoFrame.type = 0;
//			videoFrame.size = frame_size;
//			videoFrame.timestamp = xop::H264Source::GetTimestamp();
//			videoFrame.buffer.reset(new uint8_t[videoFrame.size]);
//			memcpy(videoFrame.buffer.get(), frame_buf.get(), videoFrame.size);
//			rtsp_server->PushFrame(session_id, xop::channel_0, videoFrame);
//		}
//		else {
//			break;
//		}
//
//		xop::Timer::Sleep(40);
//	};
//}
