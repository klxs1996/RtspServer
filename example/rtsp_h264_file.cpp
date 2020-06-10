#include "../example/rtsp_h264_file.h"

H264File::H264File(int buf_size)
	: m_buf_size(buf_size)
{
	m_buf = new char[m_buf_size];
}

H264File::~H264File()
{
	delete m_buf;
}

bool H264File::Open(const char *path)
{
	m_file = fopen(path, "rb");
	if (m_file == NULL) {
		return false;
	}

	const char* tmp_path = path;
	if (path && strcmp(tmp_path + strlen(tmp_path) - strlen(".h264")/*截取后缀名*/, ".h264") == 0)
	{
		file_head_size = fread(file_head, 1, 256, m_file);//读取保存了SPS,PPS的文件头
		fseek(m_file, 0, SEEK_SET);
	}

	return true;
}

void H264File::Close()
{
	if (m_file) {
		fclose(m_file);
		m_file = NULL;
		m_count = 0;
		m_bytes_used = 0;
	}
}

int H264File::ReadFrame(char* in_buf, int in_buf_size, bool* end, int& frame_type)
{
	if (m_file == NULL) {
		return -1;
	}
	frame_type = 0;
	int bytes_read = (int)fread(m_buf, 1, m_buf_size, m_file);
	if (bytes_read == 0) {
		fseek(m_file, 0, SEEK_SET);
		m_count = 0;
		m_bytes_used = 0;
		bytes_read = (int)fread(m_buf, 1, m_buf_size, m_file);
		if (bytes_read == 0) {
			this->Close();
			return -1;
		}
	}

	bool is_find_start = false, is_find_end = false;
	int i = 0, start_code = 3;
	*end = false;

	for (i = 0; i < bytes_read - 5; i++) {
		if (m_buf[i] == 0 && m_buf[i + 1] == 0 && m_buf[i + 2] == 1) {
			start_code = 3;
		}
		else if (m_buf[i] == 0 && m_buf[i + 1] == 0 && m_buf[i + 2] == 0 && m_buf[i + 3] == 1) {
			start_code = 4;
		}
		else {
			continue;
		}

		if (/*((m_buf[i + start_code] & 0x1F) == 0x7) ||
			((m_buf[i + start_code] & 0x1F) == 0x8) || */
			(((m_buf[i + start_code] & 0x1F) == 0x5 || (m_buf[i + start_code] & 0x1F) == 0x1) && ((m_buf[i + start_code + 1] & 0x80) == 0x80))) {
			if ((m_buf[i + start_code] & 0x1F) == 0x5)
				frame_type = 1;
			is_find_start = true;
			i += 4;
			break;
		}
	}

	for (; i < bytes_read - 5; i++) {
		if (m_buf[i] == 0 && m_buf[i + 1] == 0 && m_buf[i + 2] == 1)
		{
			start_code = 3;
		}
		else if (m_buf[i] == 0 && m_buf[i + 1] == 0 && m_buf[i + 2] == 0 && m_buf[i + 3] == 1) {
			start_code = 4;
		}
		else {
			continue;
		}

		if (((m_buf[i + start_code] & 0x1F) == 0x7) || ((m_buf[i + start_code] & 0x1F) == 0x8)
			|| ((m_buf[i + start_code] & 0x1F) == 0x6) || (((m_buf[i + start_code] & 0x1F) == 0x5
				|| (m_buf[i + start_code] & 0x1F) == 0x1) && ((m_buf[i + start_code + 1] & 0x80) == 0x80))) {
			is_find_end = true;
			break;
		}
	}

	bool flag = false;
	if (is_find_start && !is_find_end && m_count > 0) {
		flag = is_find_end = true;
		i = bytes_read;
		*end = true;
	}

	if (!is_find_start || !is_find_end) {
		this->Close();
		return -1;
	}

	int size = (i <= in_buf_size ? i : in_buf_size);
	memcpy(in_buf, m_buf, size);

	if (!flag) {
		m_count += 1;
		m_bytes_used += i;
	}
	else {
		m_count = 0;
		m_bytes_used = 0;
	}

	fseek(m_file, m_bytes_used, SEEK_SET);
	return size;
}

/*默认第一个关键帧之前的都属于配置信息，都读取出来*/
int H264File::ReadPPSAndSPS(char* in_buf, int in_buf_size)
{
	int index = 0;
	if (in_buf)
	{
		int start_code = 0;

		for (index = 0; index < file_head_size; index++) {
			if (file_head[index] == 0 && file_head[index + 1] == 0 && file_head[index + 2] == 1) {
				start_code = 3;
			}
			else if (file_head[index] == 0 && file_head[index + 1] == 0 && file_head[index + 2] == 0 && file_head[index + 3] == 1) {
				start_code = 4;
			}
			else {
				continue;
			}

			//必须找到关键帧才能返回
			if (((file_head[index + start_code] & 0x1F) == 0x5 || (file_head[index + start_code] & 0x1F) == 0x1) && ((file_head[index + start_code + 1] & 0x80) == 0x80)) {
				break;
			}
		}

		if (index < file_head_size && index < in_buf_size)
		{
			memset(in_buf, 0, in_buf_size);
			memcpy(in_buf, file_head, index);
		}
		else
		{
			index = 0;
		}
	}
	return index;
}

