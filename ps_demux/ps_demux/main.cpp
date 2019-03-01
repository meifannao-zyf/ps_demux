#include <stdio.h>
#include "ps_struct.h"
#include "ps_demux.h"

void __stdcall es_data(unsigned char* es_data, int es_data_len, PS_ESParam_S es_param, void* user_param)
{
	if(NULL == es_data || 0 > es_data_len)
	{
		return;
	}

	static FILE* fp_h264 = fopen("demo.h264", "wb+");
	if(NULL == fp_h264)
	{
		return;
	}
	static bool sps_write = false;
	static bool pps_write = false;

	if(PS_ES_TYPE_VIDEO == es_param.es_type)
	{
		if(false == sps_write)
		{
			if (PS_ES_FRAME_TYPE_SPS == es_param.video_param.frame_type)
			{
				fwrite(es_data, 1, es_data_len, fp_h264);
				sps_write = true;
				return;
			}
			else
			{
				return;
			}
		}

		if(false == pps_write)
		{
			if (PS_ES_FRAME_TYPE_PPS == es_param.video_param.frame_type)
			{
				fwrite(es_data, 1, es_data_len, fp_h264);
				pps_write = true;
				return;
			}
			else
			{
				return;
			}
		}

		if(	PS_ES_FRAME_TYPE_SPS == es_param.video_param.frame_type ||
			PS_ES_FRAME_TYPE_PPS == es_param.video_param.frame_type ||
			PS_ES_FRAME_TYPE_SEI == es_param.video_param.frame_type)
		{
			return;
		}

		fwrite(es_data, 1, es_data_len, fp_h264);
	}

	return;
}

int main()
{
	CParsePS parse_ps_instance;
	parse_ps_instance.init_parse();
	parse_ps_instance.set_es_callback(es_data, 0);

	char ps_data[188] = {0};
	FILE* fp = fopen("demo.ps", "rb");
	if(NULL == fp)
	{
		printf("failed to open ts file!");
		return -1;
	}

	int read_data_len = 0;
	while(read_data_len = fread(ps_data, 1, 188, fp))
	{
		if(188 != read_data_len) break;
		parse_ps_instance.put_pkt_data((unsigned char*)ps_data, 188);
	}

	return 0;
}