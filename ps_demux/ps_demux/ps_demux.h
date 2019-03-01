#pragma once

/* 解复用ps流类声明 */
class CParsePS
{
public:
	CParsePS();
	~CParsePS();

	/* 设置接口es裸流的数据回调函数 */
	int set_es_callback(es_callback es_cb, void* user_param);

	/* 初始化解复用 */
	int init_parse();

	/* 填充ps数据 */
	int put_pkt_data(unsigned char* pkt_data, int pkt_data_len);
	
	/* ps流之中是否含有音频数据 */
	bool has_audio_stream();

protected:
private:

	/* 寻找一段数据之中ps头的位置，返回值为ps头的指针，输出参数data_len为从ps头起始，剩余数据长度 */
	unsigned char* seek_ps_header(unsigned char* data, int &data_len); 

	/* 寻找一段数据之中pes头的位置，返回值为pes头的指针, 输出参数data_len为pes头起始，剩余数据长度 */
	unsigned char* seek_pes_header(unsigned char* data, int &data_len);

	/* 判断一段数据是否以视频或者音频pes头起始 */
	bool is_pes_begin(unsigned char* data, int data_len);

	/* 判断一段ps数据之中是否含有ps system header */
	bool is_ps_has_system_header(unsigned char* ps_data, int ps_data_len);

	/* 判断一段ps数据之中是否含有ps system map */
	bool is_ps_has_system_map(unsigned char* ps_data, int ps_data_len);
	
	/* 从一段ps数据之中分析音视频编码类型等信息，返回值为整个ps头的长度 */
	int update_ps_param(unsigned char* ps_data, int ps_data_len);

	/* 从一段pes数据分析该pes数据类型、音频采样率和通道数等信息 */
	int update_pes_param(unsigned char* pes_data, int pes_data_len, bool is_updata_v_pts);	//返回值为整个pes头的长度

	/* 拷贝一段es数据到本地es缓存之中 */
	int cpy_es_data_to_memory(unsigned char* es_data, int es_data_len);

	/* 拷贝一段视频es数据到本地视频es数据缓存之中 */
	int cpy_es_data_to_video_memory(unsigned char* es_data, int es_data_len);
	
	/* 拷贝一段音频es数据到本地音频es数据缓存之中 */
	int cpy_es_data_to_audio_memory(unsigned char* es_data, int es_data_len);

	/* 获取一段pes包头之中的pts */
	__int64 get_pts(unsigned char* pes_data, int pes_data_len);
	
	/* 获取一段pes包头之中的dts */
	__int64 get_dts(unsigned char* pes_data, int pes_data_len);

	/* 寻找一段数据之中0x00000001起始码开始的位置(h264 startcode)， 返回值为位置指针，输出参数data_len为剩余数据长度 */
	unsigned char* find_nalu_startcode(unsigned char* data, int &data_len);
	
	/* 寻找一段数据之中0x000001起始码开始的位置， 返回值为位置指针，输出参数data_len为剩余数据长度 */
	unsigned char* find_nalu_startcode2(unsigned char* data, int &data_len);
	
	/* 判断一个es视频帧数据是否为关键帧 */
	bool es_is_i_frame(unsigned char* es_data);

	/* 获取视频帧类型 */
	PS_ESFrameType_E get_video_frame_type(unsigned char* es_data);

	/* 处理一个完整的ps数据包数据 */
	int process_ps_data();

	/* 向上层触发es数据流回调函数 */
	int touch_es_callback();

	/* 判断一段数据是否由ps header起始 */
	bool ps_header_begin(unsigned char* data, int data_len);
	
	/* 判断一段数据是否由ps system map起始 */
	bool is_system_map(unsigned char* data, int data_len);

	/* 保存的用户数据回调和用户参数 */
	es_callback es_cb_;
	void* user_param_;

	PS_ESParam_S es_param_;				/* 当前ps音视频数据的es 参数 */
	bool is_begin_parse_;				/* 是否已经找到ps头解析出相关的编解码参数 */
	
	int video_stream_id_;				/* 从ps system map 之中分析出来的 视频pes包startcode id */
	int audio_stream_id_;				/* 从ps system map 之中分析出来的 音频pes包startcode id */
	
	PS_ESType_E reading_type_;			/* 当前正在读取的ES数据类型 */

	unsigned char* es_video_data_;		/* 本地视频es数据缓存 */
	int es_video_data_index_;			/* 缓存下来的视频es数据的长度 */

	unsigned char* es_audio_data_;		/* 本地音频es数据缓存 */
	int es_audio_data_index_;			/* 缓存下来的音频es数据的长度 */

	unsigned char* ps_data_;			/* ps数据缓存，从接收到的数据之中完整读取到一个ps数据包为止 */
	int ps_data_index_;					/*  */

	bool aac_param_update_;				/* 对于音频编码是aac的码流相关的采样率与通道数参数是否已经从adts之中分析出来 */
};