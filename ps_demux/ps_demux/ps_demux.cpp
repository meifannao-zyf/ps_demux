/* 标准库头文件 */
#include <string>

/* 私有库头文件 */
#include "ps_struct.h"
#include "ps_demux.h"

CParsePS::CParsePS():
es_cb_(NULL),
user_param_(NULL),
es_video_data_index_(0),
es_audio_data_index_(0),
is_begin_parse_(false),
video_stream_id_(-1),
audio_stream_id_(-1),
reading_type_(PS_ES_TYPE_INVALID),
ps_data_index_(0),
aac_param_update_(false),
es_video_data_(NULL),
es_audio_data_(NULL),
ps_data_(NULL)
{
	return;
}

CParsePS::~CParsePS()
{
	if(NULL != es_video_data_)
	{
		free(es_video_data_);
		es_video_data_ = NULL;
	}

	if(NULL != es_audio_data_)
	{
		free(es_audio_data_);
		es_audio_data_ = NULL;
	}

	if(NULL != ps_data_)
	{
		free(ps_data_);
		ps_data_ = NULL;
	}
}

/* 保存ES数据回调函数和用户参数 */
int CParsePS::set_es_callback(es_callback es_cb, void* user_param)
{
	es_cb_ = es_cb;
	user_param_ = user_param;

	return 0;
}

/* 初始化解析，申请本地缓冲内存 */
int CParsePS::init_parse()
{
	es_video_data_ = (unsigned char*)malloc(ES_BUFFER);
	if(NULL == es_video_data_)
	{
		return -1;
	}

	es_audio_data_ = (unsigned char*)malloc(ES_BUFFER);
	if(NULL == es_audio_data_)
	{
		return -1;
	}

	ps_data_ = (unsigned char*)malloc(PS_BUFFER);
	if(NULL == ps_data_)
	{
		return -1;
	}

	return 0;
}

/* 填充ps数据开始进行解析 */
int CParsePS::put_pkt_data(unsigned char* pkt_data, int pkt_data_len)
{
	/* 入参检查 */
	if(!pkt_data || 0 >= pkt_data_len)
	{
		return -1;
	}

	/* 从某一个含有 ps系统头 的 ps头开始处理数据，目的是解析之前要首先从ps system map 之中获得到音视频流编码类型和音视频流id */
	if(!is_begin_parse_)
	{
		/* 先找到一个ps header */
		unsigned char* ps_header_pos = seek_ps_header(pkt_data, pkt_data_len);
		if(NULL == ps_header_pos)
			return -1;

		while(NULL != ps_header_pos)
		{
			/* 循环寻找含有system map的ps packet */
			bool is_has_system_map = is_ps_has_system_map(ps_header_pos, pkt_data_len);
			if(true == is_has_system_map)
			{
				break;
			}

			ps_header_pos = seek_ps_header(ps_header_pos+4, pkt_data_len);
		}
		
		/* 当前填充数据之中没有找到含有system map 的 ps packet则直接返回失败*/
		if(NULL == ps_header_pos)
		{
			return -1;
		}

		int ps_all_header_len = update_ps_param(ps_header_pos, pkt_data_len);		//获取 stream_type, stream_id
		if(-1 == ps_all_header_len)
		{
			return -1;
		}

		/* 相关信息获取成功，开始解析ps数据 */
		pkt_data = ps_header_pos;
		is_begin_parse_ = true;
	}

	while(pkt_data_len > 0)
	{
		int ps_header_pos_len = pkt_data_len;
		unsigned char* ps_header_pos = seek_ps_header(pkt_data, ps_header_pos_len);
		if(NULL == ps_header_pos)			//整段数据没有一个ps头
		{
			if(PS_BUFFER < ps_data_index_+pkt_data_len)
			{
				return -1;
			}

			memcpy(ps_data_+ps_data_index_, pkt_data, pkt_data_len);
			ps_data_index_ += pkt_data_len;
			return 0;
		}
		else if(ps_header_pos == pkt_data)	//数据包以ps头开头
		{
			int ps_header_pos_next_len = ps_header_pos_len - 4;
			unsigned char* ps_header_pos_next = seek_ps_header(ps_header_pos + 4, ps_header_pos_next_len);	//寻找此段数据之中下一个ps头的位置
			if(NULL == ps_header_pos_next)				//整段数据不存在下一个ps头
			{
				/* 先处理当前ps缓存之中的ps数据 */
				if(0 != ps_data_index_)
				{
					int err_code = process_ps_data();
					if(-1 == err_code)
					{
						memset(ps_data_, 0, PS_BUFFER);
						ps_data_index_ = 0;
					}
				}

				/* 然后再拷贝整段数据到ps缓存之中 */
				memcpy(ps_data_+ps_data_index_, pkt_data, pkt_data_len);
				ps_data_index_ += pkt_data_len;
				return 0;
			}
			else										//存在下一个ps头
			{
				/* 先处理当前ps缓存之中的ps数据 */
				if(0 != ps_data_index_)
				{
					int err_code = process_ps_data();
					if(-1 == err_code)
					{
						memset(ps_data_, 0, PS_BUFFER);
						ps_data_index_ = 0;
					}
				}

				/* 拷贝两个ps包头之间的数据到ps缓存之中 */
				int cpy_data_len = ps_header_pos_len - ps_header_pos_next_len;
				memcpy(ps_data_+ps_data_index_, ps_header_pos, cpy_data_len);
				ps_data_index_ += cpy_data_len;

				//数据内容跳转到下一个ps包头
				pkt_data = ps_header_pos_next;
				pkt_data_len = ps_header_pos_next_len;

				continue;
			}
		}
		else								//ps头在数据包中间
		{
			/* 拷贝ps头之前的内容 */
			int cpy_data_len = pkt_data_len - ps_header_pos_len;
			if(PS_BUFFER < ps_data_index_+cpy_data_len)
			{
				return -1;
			}
			memcpy(ps_data_+ps_data_index_, pkt_data, cpy_data_len);
			ps_data_index_ += cpy_data_len;

			/* 数据跳转到下一个ps头 */
			pkt_data = ps_header_pos;
			pkt_data_len = ps_header_pos_len;
		}
	}

	return 0;
}

int CParsePS::process_ps_data()
{
	/* 对异常数据进行过滤 */
	if(!ps_data_ || ps_data_index_ <= 0)
	{
		return -1;
	}

	/* 缓存之中不是一个完成的ps数据包的时候的数据进行过滤 */
	if(!ps_header_begin(ps_data_, ps_data_index_))
	{
		return -1;
	}


	unsigned char* ps_data = ps_data_;
	int ps_data_len = ps_data_index_;

	/* 更新ps参数 */
	int ps_all_header_len = update_ps_param(ps_data, ps_data_len);		
	if(-1 == ps_all_header_len)
		return -1;

	/* 跳过ps头 */
	ps_data += ps_all_header_len;
	ps_data_len -= ps_all_header_len;

	bool is_update_video_pts = true;	//视频包会分多个pes包进行发送，仅在解析到第一个pes包的时候获取pts

	/* 处理ps payload, 每一次处理肯定要从pes头开始 */
	while(0 < ps_data_len)
	{
		/* 更新pes参数*/
		int pes_header_len = update_pes_param(ps_data, ps_data_len, is_update_video_pts);
		if(-1 == pes_header_len)
		{
			return -1;
		}

		/* 跳过pes头 */
		ps_data += pes_header_len;
		ps_data_len -= pes_header_len;

		/* 寻找下一个pes的位置 */
		int pes_header_next_len = ps_data_len;
		unsigned char* pes_header_next = seek_pes_header(ps_data, pes_header_next_len);
		if(NULL == pes_header_next)							//没有下一个pes数据包了
		{
			/* 直接拷贝当前数据 */
			cpy_es_data_to_memory(ps_data, ps_data_len);
			break;
		}
		else												//找到了下一个pes数据包
		{
			/* 拷贝两个pes之间的数据 */
			int cpy_data_len = ps_data_len - pes_header_next_len;
			cpy_es_data_to_memory(ps_data, cpy_data_len);

			/* 跳转到下一个pes的位置 */
			ps_data = pes_header_next;
			ps_data_len = pes_header_next_len;

			is_update_video_pts = false;
		}
	}

	/* 触发数据回调函数，向上回调视频数据 */
	int err_code = touch_es_callback();
	if(-1 == err_code)
	{
		return -1;
	}

	memset(ps_data_, 0, PS_BUFFER);
	ps_data_index_ = 0;

	return 0;
}

/* 向上层触发数据流回调函数 */
int CParsePS::touch_es_callback()
{
	if(-1 != video_stream_id_ && 0 != es_video_data_index_)	//缓存之中必须需要有数据方可回调
	{
		unsigned char* es_all_v = es_video_data_;
		int es_all_len = es_video_data_index_;

		do
		{
			int remain_data_len_f = es_all_len;
			unsigned char* nalu_head_first = find_nalu_startcode(es_all_v, remain_data_len_f);
			if(NULL == nalu_head_first)
			{
				break;
			}

			while(5 < remain_data_len_f)	//nalu head len
			{
				/* 过滤掉不关心的数据 */
				if(0x09 == nalu_head_first[4])	//宇视码流有0x0000000109分隔符
				{
					nalu_head_first += 4;
					remain_data_len_f -= 4;
					nalu_head_first = find_nalu_startcode(nalu_head_first, remain_data_len_f);
					if(NULL == nalu_head_first)
					{
						break;
					}
				}

				int remain_data_len_s = remain_data_len_f-4;
				unsigned char* nalu_head_second = find_nalu_startcode(nalu_head_first+4, remain_data_len_s);
				if(NULL == nalu_head_second)
				{
					/* 触发视频流回调 */
					es_param_.video_param.is_i_frame = es_is_i_frame(nalu_head_first);
					es_param_.video_param.frame_type = get_video_frame_type(nalu_head_first);
					es_param_.es_type = PS_ES_TYPE_VIDEO;
					if(NULL != es_cb_)
						es_cb_(nalu_head_first, remain_data_len_f, es_param_, user_param_);

					break;
				}
				else
				{
					/* 先触发视频流回调，然后从下一帧开始分析 */
					es_param_.video_param.is_i_frame = es_is_i_frame(nalu_head_first);
					es_param_.video_param.frame_type = get_video_frame_type(nalu_head_first);
					es_param_.es_type = PS_ES_TYPE_VIDEO;

					int touch_data_len = remain_data_len_f - remain_data_len_s;
					if(NULL != es_cb_)
						es_cb_(nalu_head_first, touch_data_len, es_param_, user_param_);

					nalu_head_first = nalu_head_second;
					remain_data_len_f = remain_data_len_s;
				}

			}

		}while(false);

		memset(es_video_data_, 0, ES_BUFFER);
		es_video_data_index_ = 0;
		es_param_.video_param.is_i_frame = false;
	}
	
	if(-1 != audio_stream_id_&& 0 != es_audio_data_index_)
	{
		/* 触发音频流回调 */
		es_param_.es_type = PS_ES_TYPE_AUDIO;

		if(es_cb_)
			es_cb_(es_audio_data_, es_audio_data_index_, es_param_, user_param_);

		memset(es_audio_data_, 0, ES_BUFFER);
		es_audio_data_index_ = 0;
	}

	return 0;
}

/* 校验一段数据是否以 ps header(0x000001BA)开始 */
bool CParsePS::ps_header_begin(unsigned char* data, int data_len)
{
	if(!data || data_len <= 0)
	{
		return false;
	}

	if((0x00 == data[0]) && (0x00 == data[1]) && (0x01 == data[2]) && (0xBA == data[3]))
	{
		return true;
	}

	return false;
}

/* 寻找一段数据之中ps header(0x000001BA)的位置 */
unsigned char* CParsePS::seek_ps_header(unsigned char* data, int &data_len)
{
	if(!data || data_len <= 0)
		return NULL;

	data += 3;
	data_len -= 3;

	while(data_len > 0)
	{
		if(*data == 0xBA)
		{
			if(*(data-3) == 0x00 && *(data-2) == 0x00 && *(data-1) == 0x01)
			{
				data_len += 3;
				return data-3;
			}

			data += 4;
			data_len -= 4;

			continue;
		}

		data += 1;
		data_len -= 1;
	}

	return NULL;
}

/* 寻找一段数据之中pes header(0x000001 stream id)的位置 */
unsigned char* CParsePS::seek_pes_header(unsigned char* data, int &data_len)
{
	if(!data || data_len <= 0)
		return NULL;

	data += 3;
	data_len -= 3;

	while(data_len > 0)
	{
		if(*data == video_stream_id_ || *data == audio_stream_id_)
		{
			if(*(data-3) == 0x00 && *(data-2) == 0x00 && *(data-1) == 0x01)
			{
				data_len += 3;
				return data-3;
			}

			data += 4;
			data_len -= 4;

			continue;
		}
		else
		{
			data += 1;
			data_len -= 1;
		}
	}

	return NULL;
}

/* 校验一段ps数据之中是否含有ps system header(0x000001BB) */
bool CParsePS::is_ps_has_system_header(unsigned char* ps_data, int ps_data_len)
{
	if(!ps_data || ps_data_len <= 0)
		return false;

	/* 如果有system header应该紧跟在ps header之后，这个地方跳过ps header len  */
	int pack_stuffing_length = ps_data[13]&0x07;	//ps头后的填充字节长度
	int ps_header_len = 14 + pack_stuffing_length;	//ps头填充字节之前一共14个字节

	/* 校验ps header之后紧接着的数据是否为 system header */
	if(ps_data[ps_header_len] == 0x00 && ps_data[ps_header_len+1] == 0x00 && ps_data[ps_header_len+2] == 0x01 && ps_data[ps_header_len+3] == 0xBB)
		return true;

	return false;
}

/* 校验一段ps数据之中是否含有ps system map(0x000001BC) */
bool CParsePS::is_ps_has_system_map(unsigned char* ps_data, int ps_data_len)
{
	if(!ps_data || ps_data_len <= 0)
		return false;

	/* 跳过ps header */
	int pack_stuffing_length = ps_data[13]&0x07;	//ps头后的填充字节长度
	int ps_header_len = 14 + pack_stuffing_length;	//ps头填充字节之前一共14个字节

	/* 跳过 system header */
	int ps_system_header_len = 0;
	bool is_sys_h = is_ps_has_system_header(ps_data, ps_data_len);	
	if(!is_sys_h)			//不存在系统头
	{
		ps_system_header_len = 0;
	}
	else					//存在系统头
	{
		ps_system_header_len = (ps_data[ps_header_len+4]<<8) + ps_data[ps_header_len+5] + 6;	//system header长度
	}

	/* 校验ps system map是否存在 */
	unsigned char* ps_sytem_map_data = ps_data + ps_header_len + ps_system_header_len;
	int ps_system_map_len_before = ps_data_len - ps_header_len - ps_system_header_len;
	bool is_sys_map = is_system_map(ps_sytem_map_data, ps_system_map_len_before);

	return is_sys_map;
}

/* 解析ps包之中 stream encode type, stream id 返回值为ps header + ps system header + ps system map长度 */
int CParsePS::update_ps_param(unsigned char* ps_data, int ps_data_len)
{
	/* 入参校验 */
	if(!ps_data || ps_data_len <= 0)
		return -1;

	/* ps header 长度 */
	int pack_stuffing_length = ps_data[13]&0x07;	//ps头后的填充字节长度
	int ps_header_len = 14 + pack_stuffing_length;	//ps头填充字节之前一共14个字节

	/* ps system mheader 长度 */
	int ps_system_header_len = 0;
	bool is_sys_h = is_ps_has_system_header(ps_data, ps_data_len);	
	if(!is_sys_h)			//不存在系统头
	{
		ps_system_header_len = 0;
	}
	else					//存在系统头
	{
		ps_system_header_len = (ps_data[ps_header_len+4]<<8) + ps_data[ps_header_len+5] + 6;	//system header长度
	}

	/* ps system map 长度 */
	unsigned char* ps_sytem_map_data = ps_data + ps_header_len + ps_system_header_len;
	int ps_system_map_len_before = ps_data_len - ps_header_len - ps_system_header_len;
	bool is_sys_map = is_system_map(ps_sytem_map_data, ps_system_map_len_before);
	if(false == is_sys_map)
	{
		return ps_header_len + ps_system_header_len;
	}

	int ps_system_map_es_len_index = (ps_sytem_map_data[8]<<8) + ps_sytem_map_data[9] + 10;
	int ps_system_map_es_info_index = ps_system_map_es_len_index + 2;

	/* 计算循环，在map之中解析 stream num , stream encode type, stream id */
	int cycle_len = (ps_sytem_map_data[ps_system_map_es_len_index]<<8) + ps_sytem_map_data[ps_system_map_es_len_index+1];
	while(cycle_len > 0)
	{
		if(0x1B == ps_sytem_map_data[ps_system_map_es_info_index])			//h264
		{
			video_stream_id_ = ps_sytem_map_data[ps_system_map_es_info_index+1];
			es_param_.video_param.video_encode_type = PS_VIDEO_ENCODE_H264;
		}
		else if(0x90 == ps_sytem_map_data[ps_system_map_es_info_index])		//g711 通道数和采样率是固定值
		{
			audio_stream_id_ = ps_sytem_map_data[ps_system_map_es_info_index+1];
			es_param_.audio_param.audio_encode_type = PS_AUDIO_ENCODE_PCMA;
			es_param_.audio_param.channels = 1;
			es_param_.audio_param.samples_rate = 8000;
		}
		else if (0x0F == ps_sytem_map_data[ps_system_map_es_info_index])	//AAC pes param的时候会更新掉
		{
			audio_stream_id_ = ps_sytem_map_data[ps_system_map_es_info_index+1];
			es_param_.audio_param.audio_encode_type = PS_AUDIO_ENCODE_AAC;
		}
		else
		{
			/* unknow stream type */
		}

		int high_byte_len = ps_sytem_map_data[ps_system_map_es_info_index+2]<<8;
		int low_byte_len = ps_sytem_map_data[ps_system_map_es_info_index+3];

		ps_system_map_es_info_index += high_byte_len + low_byte_len + 2 + 2;
		cycle_len -= (high_byte_len + low_byte_len + 2 + 2);
	}

	int ps_system_map_len = (ps_sytem_map_data[4]<<8) + ps_sytem_map_data[5] + 6;

	return ps_header_len + ps_system_header_len + ps_system_map_len;
}

/* 更新pes参数，返回 pes header len */
int CParsePS::update_pes_param(unsigned char* pes_data, int pes_data_len, bool is_updata_v_pts)
{
	if(!pes_data || pes_data_len < 9)
		return -1;

	if(!(pes_data[0] == 0x00 && pes_data[1] == 0x00 && pes_data[2] == 0x01))	//非pes过滤
		return -1;

	if(video_stream_id_ == pes_data[3])	//视频流更新数据类型，获取pts
	{
		reading_type_ = PS_ES_TYPE_VIDEO;
		if(is_updata_v_pts)
		{
			es_param_.video_param.pts = get_pts(pes_data, pes_data_len);
		}
	}
	else if(audio_stream_id_ == pes_data[3])
	{
		/* 对于aac 码流解析 aac data adts header, 从中分析采样率和通道数, aac_param_update_ 标识是否已经解析出 aac 的采样率和通道数，解析成功过了就不再解析 */
		if(!aac_param_update_ && es_param_.audio_param.audio_encode_type == PS_AUDIO_ENCODE_AAC)
		{
			unsigned int es_begin_distance = pes_data[8]+9;
			if(es_begin_distance+2 > (unsigned int)pes_data_len)		//长度不正常的数据包过滤
				return -1;

			if(!(0xFF == (pes_data[es_begin_distance]&0xFF) && 0xF0 == (pes_data[es_begin_distance]&0xF0)))		//adts startcode
			{
				return -1;
			}

			/* adts 获取采样率 */
			int frequency = (pes_data[es_begin_distance+2]&0x3C)>>2;
			if(0 == frequency)
			{
				es_param_.audio_param.samples_rate = 96000;
			}
			else if(1 == frequency)
			{
				es_param_.audio_param.samples_rate = 88200;
			}
			else if(2 == frequency)
			{
				es_param_.audio_param.samples_rate = 64000;
			}
			else if(3 == frequency)
			{
				es_param_.audio_param.samples_rate = 48000;
			}
			else if(4 == frequency)
			{
				es_param_.audio_param.samples_rate = 44100;
			}
			else if(5 == frequency)
			{
				es_param_.audio_param.samples_rate = 32000;
			}
			else if(6 == frequency)
			{
				es_param_.audio_param.samples_rate = 24000;
			}
			else if(7 == frequency)
			{
				es_param_.audio_param.samples_rate = 22050;
			}
			else if(8 == frequency)
			{
				es_param_.audio_param.samples_rate = 16000;
			}
			else if(9 == frequency)
			{
				es_param_.audio_param.samples_rate = 12000;
			}
			else if(10 == frequency)
			{
				es_param_.audio_param.samples_rate = 11025;
			}
			else if(11 == frequency)
			{
				es_param_.audio_param.samples_rate = 8000;
			}
			else if(12 == frequency)
			{
				es_param_.audio_param.samples_rate = 7350;
			}
			else
			{
				return -1;
			}

			/* adts 获取通道数 */

			int channles = ((pes_data[es_begin_distance+2]&0x01)<<2) + ((pes_data[es_begin_distance+3]&0xC0)>>6);
			if(1 == channles)
			{
				es_param_.audio_param.channels = 1;
			}
			else if(2 == channles)
			{
				es_param_.audio_param.channels = 2;
			}
			else if(3 == channles)
			{
				es_param_.audio_param.channels = 3;
			}
			else if(4 == channles)
			{
				es_param_.audio_param.channels = 4;
			}
			else if(5 == channles)
			{
				es_param_.audio_param.channels = 5;
			}
			else if(6 == channles)
			{
				es_param_.audio_param.channels = 6;
			}
			else if(7 == channles)
			{
				es_param_.audio_param.channels = 8;
			}
			else
			{
				return -1;
			}

			aac_param_update_ = true;
		}

		/* 音频流更新数据类型，获取pts */
		reading_type_ = PS_ES_TYPE_AUDIO;
		es_param_.audio_param.pts = get_pts(pes_data, pes_data_len);
	}
	else
	{
		return -1;		
	}

	return pes_data[8]+9;
}

/* 获取pts */
__int64 CParsePS::get_pts(unsigned char* pes_data, int pes_data_len)
{
	pes_data += 9;		//偏移至pts所在位置
	return (unsigned __int64)(*pes_data & 0x0e) << 29 |
		(AV_RB16(pes_data + 1) >> 1) << 15 |
		AV_RB16(pes_data + 3) >> 1;
}

/* 获取dts */
__int64 CParsePS::get_dts(unsigned char* pes_data, int pes_data_len)
{
	pes_data += 14;		//偏移至pts所在位置
	return (unsigned __int64)(*pes_data & 0x0e) << 29 |
		(AV_RB16(pes_data + 1) >> 1) << 15 |
		AV_RB16(pes_data + 3) >> 1;
}

/* 查找一段数据之中的0x00000001位置，返回值为位置指针，输出参数data_len为剩余数据长度 */
unsigned char* CParsePS::find_nalu_startcode(unsigned char* data, int &data_len)
{
	if(!data || data_len < 4)
		return NULL; 

	data += 3;
	data_len -= 3;

	while(data_len >= 0)
	{
		if(*data == 0x01)
		{
			if(0x00 == *(data-1) && 0x00 == *(data-2) && 0x00 == *(data-3))
			{
				data_len += 3;	//因为数据位置向前移动3个字节，所以data_len应该加3
				return data-3;	
			}
			else
			{
				data += 4;
				data_len -= 4;
				continue;
			} 	
		}
		data += 1;
		data_len -= 1;
	}

	return NULL;
}

/* 查找一段数据之中的0x000001位置，返回值为位置指针，输出参数data_len为剩余数据长度 */
unsigned char* CParsePS::find_nalu_startcode2(unsigned char* data, int &data_len)
{
	if(!data || data_len < 3)
		return NULL; 

	data += 2;
	data_len -= 2;

	while(data_len >= 0)
	{
		if(*data == 0x01)
		{
			if(0x00 == *(data-1) && 0x00 == *(data-2))
			{
				data_len += 2;	//因为数据位置向前移动3个字节，所以data_len应该加3
				return data-2;	
			}
			else
			{
				data += 3;
				data_len -= 3;
				continue;
			} 	
		}
		data += 1;
		data_len -= 1;
	}

	return NULL;
}

/* 拷贝es数据到本地es缓存 */
int CParsePS::cpy_es_data_to_memory(unsigned char* es_data, int es_data_len)
{
	if(reading_type_ == PS_ES_TYPE_VIDEO)
		return cpy_es_data_to_video_memory(es_data, es_data_len);
	else if(reading_type_ == PS_ES_TYPE_AUDIO)
		return cpy_es_data_to_audio_memory(es_data, es_data_len);
	else
		return -1;
}

/* 拷贝es数据到视频es缓存 */
int CParsePS::cpy_es_data_to_video_memory(unsigned char* es_data, int es_data_len)
{
	if(!es_data || es_data_len <= 0)
		return -1;

	int cpy_data_len = 0;

	if(es_data_len <= ES_BUFFER - es_video_data_index_)
	{
		memcpy(es_video_data_+es_video_data_index_, es_data, es_data_len);
		es_video_data_index_ += es_data_len;
		cpy_data_len += es_data_len;

		return cpy_data_len;
	}

	return cpy_data_len;
}

/* 拷贝es数据到音频数据缓存 */
int CParsePS::cpy_es_data_to_audio_memory(unsigned char* es_data, int es_data_len)
{
	if(!es_data || es_data_len <= 0)
		return -1;

	if(es_data_len <= ES_BUFFER - es_audio_data_index_)
	{
		memcpy(es_audio_data_+es_audio_data_index_, es_data, es_data_len);
		es_audio_data_index_ += es_data_len;
		return es_data_len;
	}

	return -1;
}

/* I帧判断 */
bool CParsePS::es_is_i_frame(unsigned char* es_data)
{
	return ((es_data[4]&0x1F) == 5 || (es_data[4]&0x1F) == 2|| (es_data[4]&0x1F) == 7|| (es_data[4]&0x1F) == 8) ? true : false;
}

/* 获取视频帧类型 */
PS_ESFrameType_E CParsePS::get_video_frame_type(unsigned char* es_data)
{
	int type = es_data[4]&0x1F;
	switch(type){
	case 2:
		return PS_ES_FRAME_TYPE_DATA;
	case 5:
		return PS_ES_FRAME_TYPE_IDR;
	case 6:
		return PS_ES_FRAME_TYPE_SEI;
	case 7:
		return PS_ES_FRAME_TYPE_SPS;
	case 8:
		return PS_ES_FRAME_TYPE_PPS;
	}
	return PS_ES_FRAME_TYPE_INVALID;
}

/* 校验一段数据是否以pes header(0x000001 stream id)起始 */
bool CParsePS::is_pes_begin(unsigned char* data, int data_len)
{
	if(!data || data_len <= 0)
		return false;

	/* 视频pes */
	if((0x00 == data[0]) && (0x00 == data[1]) && (0x01 == data[2]) && (video_stream_id_ == data[3]))
		return true;

	/* 音频pes */
	if((0x00 == data[0]) && (0x00 == data[1]) && (0x01 == data[2]) && (audio_stream_id_ == data[3]))
		return true;

	return false;
}

/* 校验一段数据是否以ps system map(0x000001BC)起始 */
bool CParsePS::is_system_map(unsigned char* data, int data_len)
{
	if(!data || data_len <= 3)
		return false;

	if((0x00 == data[0]) && (0x00 == data[1]) && (0x01 == data[2]) && (0xBC == data[3]))
	{
		return true;
	}

	return false;
}

/* 根据audio_stream_id是否得到更新来校验数据之中是否含有音频数据 */
bool CParsePS::has_audio_stream()
{
	if(-1 == audio_stream_id_)
		return false;

	return true;
}