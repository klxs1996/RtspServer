// RTSP Server

#include "xop/RtspServer.h"
#include "net/Timer.h"

class H264File
{
public:
	H264File(int buf_size=500000);
	~H264File();

	bool Open(const char *path);
	void Close();

	bool IsOpened() const
	{ return (m_file != NULL); }

	int ReadFrame(char* in_buf, int in_buf_size, bool* end,int& frame_type);
	int ReadPPSAndSPS(char* in_buf, int in_buf_size);
    
private:
	FILE *m_file = NULL;
	char file_head[256] = { 0 };
	int  file_head_size = 0;
	char *m_buf = NULL;
	int  m_buf_size = 0;
	int  m_bytes_used = 0;
	int  m_count = 0;
};


