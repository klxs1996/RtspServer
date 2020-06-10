//// RTSP Pusher
//
//#include "xop/RtspPusher.h"
//#include "net/Timer.h"
//#include <thread>
//#include <memory>
//#include <iostream>
//#include <string>
//#include "../example/rtsp_h264_file.h"
//
//#define PUSH_TEST "rtsp://127.0.0.1:554/live" 
//
//void sendFrameThread(xop::RtspPusher* rtspPusher);
//
//int main(int argc, char **argv)
//{	
//	std::shared_ptr<xop::EventLoop> event_loop(new xop::EventLoop());  
//	std::shared_ptr<xop::RtspPusher> rtsp_pusher = xop::RtspPusher::Create(event_loop.get());
//
//	xop::MediaSession *session = xop::MediaSession::CreateNew(); 
//	session->AddSource(xop::channel_0, xop::H264Source::CreateNew()); 
//	session->AddSource(xop::channel_1, xop::AACSource::CreateNew(44100, 2, false));
//	rtsp_pusher->AddSession(session);
//
//	std::string url = argc >= 2 ? argv[1] : PUSH_TEST;
//
//	if (rtsp_pusher->OpenUrl(url.c_str(), 3000) < 0) {
//		std::cout << "Open " << url.c_str() << " failed." << std::endl;
//		getchar();
//		return 0;
//	}
//
//	std::cout << "Push stream to " << url << " ..." << std::endl;
//        
//	std::thread thread(sendFrameThread, rtsp_pusher.get()); 
//	thread.detach();
//
//	while (1) {
//		xop::Timer::Sleep(100);
//	}
//
//	getchar();
//	return 0;
//}
//
//void sendFrameThread(xop::RtspPusher* rtsp_pusher)
//{
//	H264File h264_file;
//	if (!h264_file.Open("D:/test/Video/1080p.h264")) {
//		printf("Open %s failed.\n", "");
//		return ;
//	}
//	std::shared_ptr<char> file_head(new char[256]);
//
//	int res = h264_file.ReadPPSAndSPS(file_head.get(), 256);
//
//	int buf_size = 2000000;
//	std::unique_ptr<uint8_t> frame_buf(new uint8_t[buf_size]);
//	bool end = false;
//	int frame_type = 0;
//	int frame_count = 0;
//	
//	while(rtsp_pusher->IsConnected())
//	{   
//		int size = h264_file.ReadFrame((char*)frame_buf.get(), buf_size, &end, frame_type);
//		
//		if (++frame_count % 50 == 0 && res != 0)
//		{
//			xop::AVFrame headFrame = { 0 };
//			headFrame.type = 0;
//			headFrame.size = res;
//			headFrame.timestamp = xop::H264Source::GetTimestamp();
//			headFrame.buffer.reset(new uint8_t[res]);
//			memcpy(headFrame.buffer.get(), file_head.get(), res);
//			rtsp_pusher->PushFrame(xop::channel_0, headFrame);
//			xop::Timer::Sleep(20);
//		}
//
//		if(size > 0)
//		{                              
//			xop::AVFrame videoFrame = { 0 };
//			videoFrame.type = 0;
//			videoFrame.size = size;
//			videoFrame.timestamp = xop::H264Source::GetTimestamp();
//			videoFrame.buffer.reset(new uint8_t[videoFrame.size]);
//			
//			memcpy(videoFrame.buffer.get(), frame_buf.get(), videoFrame.size);
//			rtsp_pusher->PushFrame(xop::channel_0, videoFrame);
//		}
//                
//		{				                       
//			/*
//				//获取一帧 AAC, 打包
//				xop::AVFrame audioFrame = {0};
//				//audioFrame.size = audio frame size;  // 音频帧大小 
//				audioFrame.timestamp = xop::AACSource::GetTimestamp(44100); // 时间戳
//				audioFrame.buffer.reset(new uint8_t[audioFrame.size]);
//				//memcpy(audioFrame.buffer.get(), audio frame data, audioFrame.size);
//
//				rtsp_pusher->PushFrame(xop::channel_1, audioFrame); //推流到服务器, 接口线程安全
//			*/
//		}		
//
//		xop::Timer::Sleep(40); 
//	}
//}
