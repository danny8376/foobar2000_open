#include "Encoders.h"
#include "Config.h"

#include "../SDK/foobar2000.h"
#include "../shared/shared.h"

#define COMPONENT_NAME "Vorbis Streamer"
#define COMPONENT_VERSION_BASE "1.1"
#ifdef DEBUG
#define COMPONENT_VERSION COMPONENT_VERSION_BASE"-debug"
#else
#define COMPONENT_VERSION COMPONENT_VERSION_BASE
#endif
#define COMPONENT_ABOUT COMPONENT_NAME"\nBy Canar\n\nModified from the sources for edcast v3 by oddsock@oddsock.org"
DECLARE_COMPONENT_VERSION(COMPONENT_NAME,COMPONENT_VERSION,COMPONENT_ABOUT);

//globals
stream_encoders encoders;
pfc::counter dsp_instances;

class dsp_vstream:public dsp_impl_base{
	stream_encoder* encoder;

	metadb_handle::ptr curTrack;

public:
	dsp_vstream(const dsp_preset& p_data){
		dsp_instances++;
//edcastGlobals*g=(edcastGlobals*)p_data.get_data();
//console::formatter() << "A ? C:" << g->gShoutcastFlag << " " << g->gIcecast2Flag;
		encoder=encoders.attach(p_data,this);
	}
	~dsp_vstream(){
		encoder->detach();
		dsp_instances--;
	}
	static void g_get_name(pfc::string_base&p_out){p_out=COMPONENT_NAME;}
	virtual bool on_chunk(audio_chunk*p_chunk,abort_callback&p_abort){

//#if 0 
		// enable this to send metadata from DSP rather than from playback callbacks, more accurate
		// but because this code is completely retarded, all encoder instances will get new metadata while just our one should, so this is disabled for now.

		// it should send only one metadata once now (record lastTrack within encoders)
		//metadb_handle::ptr curTrack;
		if (get_cur_file(curTrack) && curTrack != encoders.m_lastTrack/*curTrack != m_lastTrack*/) {
			//m_lastTrack = curTrack;
			encoders.m_lastTrack = curTrack;
			file_info_impl info;
			if (curTrack->get_info_async(info)) {
				encoders.update_metadata(info);
			}
		}
//#endif
		const unsigned targetRate = encoder->get_sample_rate();
		const unsigned sourceRate = p_chunk->get_sample_rate();
		if (m_resampler.is_empty() && sourceRate != targetRate) {
			if (!resampler_entry::g_create(m_resampler, sourceRate, targetRate, 0)) throw exception_io_data("Sample rate mismatch, no resampler present");
		}
		if (m_resampler.is_valid()) {
			dsp_chunk_list_impl chunks; chunks.add_chunk(p_chunk);
			m_resampler->run_abortable(&chunks, NULL, 0, p_abort);
			for(t_size walk = 0; walk < chunks.get_count(); ++walk) encoder->handle_chunk(chunks.get_item(walk));
		} else {
			encoder->handle_chunk(p_chunk);
		}
		return true;
	}

	virtual void flush() {if (m_resampler.is_valid()) m_resampler->flush();}
	virtual void on_endoftrack(abort_callback&p_abort){}
	virtual void on_endofplayback(abort_callback&p_abort){}
	virtual double get_latency(){return 0.0;}
	virtual bool need_track_change_mark(){return false;}
	static GUID g_get_guid(){
		static const GUID guid={0xceee844a,0xce07,0x4d0a,{0x95,0x87,0x34,0x7e,0x54,0x2,0xf2,0x4c}};
		return guid;
	}

	static bool g_get_default_preset(dsp_preset & p_out){
		pfc::ptrholder_t<edcastGlobals> l_globals = new edcastGlobals; // could just do "edcastGlobals l_globals;" but it's huge and ugly to put on stack
		initializeGlobals(&*l_globals);
		p_out.set_owner(g_get_guid());
		p_out.set_data(&*l_globals,sizeof(edcastGlobals));
		return true;
	}

	static bool g_have_config_popup(){
		return true;
	}

	static void g_show_config_popup(const dsp_preset & p_data, HWND p_parent, dsp_preset_edit_callback & p_callback) {
		CConfig config_popup(p_callback);
		pfc::ptrholder_t<edcastGlobals> l_globals = new edcastGlobals; // could just do "edcastGlobals l_globals;" but it's huge and ugly to put on stack
		if(p_data.get_data_size()==sizeof(edcastGlobals)){
			memcpy(&*l_globals,p_data.get_data(),sizeof(edcastGlobals));
		}else{
			initializeGlobals(&*l_globals);
		}
		
		config_popup.globals=&*l_globals;
		config_popup.data_owner=g_get_guid();
		config_popup.DoModal();
	}

	dsp::ptr m_resampler;
	//metadb_handle::ptr m_lastTrack;
};

class vstream_play_callback_ui:public play_callback_static{
public:
	virtual unsigned get_flags(){
		return /* play_callback::flag_on_playback_dynamic_info_track|	
			play_callback::flag_on_playback_new_track| */ 
			play_callback::flag_on_playback_starting| 
			play_callback::flag_on_playback_stop|
			play_callback::flag_on_playback_pause /* |
			play_callback::flag_on_playback_edited */;
	}

	virtual void on_playback_pause(bool p_state){
		if(p_state)
			encoders.disconnect();
		else
			encoders.connect();
	}

	/*
	virtual void on_playback_new_track(metadb_handle_ptr p_track) {encoders.update_metadata(p_track);}
	virtual void on_playback_edited(metadb_handle_ptr p_track){encoders.update_metadata(p_track);}
	virtual void on_playback_dynamic_info_track(const file_info&p_info){encoders.update_metadata(p_info);}
	*/
	virtual void on_playback_new_track(metadb_handle_ptr p_track) {}
	virtual void on_playback_edited(metadb_handle_ptr p_track){}
	virtual void on_playback_dynamic_info_track(const file_info&p_info){}

	virtual void on_playback_starting(play_control::t_track_command p_command,bool p_paused){on_playback_pause(p_paused);}
	virtual void on_playback_stop(play_control::t_stop_reason p_reason){
		if(p_reason != play_control::stop_reason_starting_another) encoders.disconnect();
	}


	virtual void on_playback_seek(double p_time){}
	virtual void on_playback_dynamic_info(const file_info & p_info){}
	virtual void on_playback_time(double p_time){}
	virtual void on_volume_change(float p_new_val){}
};

static dsp_factory_t<dsp_vstream>vstream_dsp;
static service_factory_single_t<vstream_play_callback_ui>vstream_callback;

static const GUID acfg_branch_guid={0x41ab6031,0xb806,0x4616,{0x85,0x37,0x45,0xa3,0xb3,0x21,0x9,0xa0}};
static const GUID acfg_logging_guid={0xef606fb0,0x2099,0x464d,{0x95,0x6e,0x7e,0x37,0xdc,0x79,0xdc,0xe8}};
static const GUID acfg_logging_error_guid={0xb10e8275,0xdcb9,0x477b,{0x98,0xb4,0x75,0xc4,0xac,0xac,0x7b,0xb8}};
static const GUID acfg_logging_info_guid={0x4ae4ec2b,0x97a2,0x4b9d,{0x9b,0xed,0xc2,0xdf,0xf8,0xab,0xf5,0xbe}};
static const GUID acfg_logging_debug_guid={0xa2512a72,0x36cb,0x4e7a,{0x88,0xd7,0xb0,0xc1,0xcb,0x7,0xe4,0xfc}};

static advconfig_branch_factory acfg_branch(COMPONENT_NAME,acfg_branch_guid,advconfig_entry::guid_branch_tools,0);
static advconfig_branch_factory acfg_logging("Log to console",acfg_logging_guid,acfg_branch_guid,0);
advconfig_radio_factory acfg_logerror("Errors",acfg_logging_error_guid,acfg_logging_guid,0,true);
advconfig_radio_factory acfg_loginfo("Information",acfg_logging_info_guid,acfg_logging_guid,0,false);
advconfig_radio_factory acfg_logdebug("Debug",acfg_logging_debug_guid,acfg_logging_guid,0,false);