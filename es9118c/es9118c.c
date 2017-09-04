/*
 * es9118.c -- es9118 ALSA SoC audio driver
 * es9118的四种工作模式 开机启动初始化，无Music时进入Standby，有音乐就如Normal,电话进入bypass
 * 切换原理：开机probe时即进执行：开机启动初始化+无Music时进入Standby
 **********：播放Music时由Standby进入Normal播放Music，音乐播放结束切回Standby
 **********：打电话由Standby进入Bypass通电话，电话结束由Bypass切回Standby
 **********：默认待机状态即为Standby，Standby（Workstatus=1）Normal（Workstatus=2）Bypass（Workstatus=3）
 *           未初始化时默认值（Workstatus=0）
 */

#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/init.h>
#include <linux/slab.h>
#include <linux/delay.h>
#include <linux/pm.h>
#include <linux/platform_device.h>
#include <linux/device.h>
#include <linux/printk.h>
#include <linux/ratelimit.h>
#include <linux/wait.h>
#include <linux/regulator/consumer.h>
#include <sound/core.h>
#include <sound/pcm.h>
#include <sound/pcm_params.h>
#include <sound/soc.h>
#include <sound/initval.h>
#include <sound/tlv.h>
#include <trace/events/asoc.h>
#include <linux/of_gpio.h>
#include <linux/gpio.h>
#include <linux/i2c.h>
#include <linux/kernel.h>
#include <linux/regmap.h>
#include <linux/workqueue.h>  
#include <linux/sched.h>  
#include <linux/init.h>  
#include <linux/interrupt.h>  
#include <linux/delay.h>  
#include <linux/proc_fs.h>   
#include <linux/fb.h>


#include "es9118c.h"

#define MT_SOC_DACES_NAME  "dac_es9118c"

#define INPUT_CONFIG_SOURCE 1
#define I2S_BIT_FORMAT_MASK (0x03 << 6)
#define MASTER_MODE_CONTROL 10
#define I2S_CLK_DIVID_MASK (0x03 << 5)
#define RIGHT_CHANNEL_VOLUME_15 15
#define LEFT_CHANNEL_VOLUME_16 16
#define MASTER_TRIM_VOLUME_17 17
#define MASTER_TRIM_VOLUME_18 18
#define MASTER_TRIM_VOLUME_19 19
#define MASTER_TRIM_VOLUME_20 20
#define HEADPHONE_AMPLIFIER_CONTROL 42

static DEFINE_MUTEX(es9118c_access);

struct kobject *debug_kobj;

/* codec private data */
struct es9118_priv {
	struct snd_soc_codec *codec;
	struct i2c_client *i2c_client;
	struct delayed_work sleep_work;
	struct mutex power_lock;
} es9118_priv;

struct es9118_reg {
	unsigned char num;
	unsigned char value;
};


#define ES9118_I2C_NAME		"ES9118_DAC" 
#define ES9118_I2C_BUS		5
#define ES9118_I2C_ADDR		0x48

static const struct i2c_device_id es9118_i2c_id[] = {
	{ES9118_I2C_NAME, 0}, 
	{ } 
};                 

static const struct of_device_id es9118_match_table[] = {
	{.compatible = "mediatek,es9118_dac" },        // "mediatek,ext_dac";
	{},
};

//No Headset-Check-default
unsigned char reg_default[34] = {0x0,0x8C,0x34,0x40,  0x0,0x68,0x42,0x80,   0xDD,0x22,0x02,0x0,  0x5A,0x40,0x0A,0x50, \                             
	          0x50,0xFF,0xFF,0xFF,  0x7F,0x0,0x0,0x0,   0x0,0x0,0x62,0xD4,  0xF0,0x0,0x0,0x0,  0x0,0x3C};

/*5-plug in  7-plug out  */
static int headset_status = 0;

/*0-No Probe  1-Probe,  
 2-Mute(Power on,Wait for 3), 3-Hifi(Power on), 
 4-ByPass(Power On), 5-StandBy(Suspend) */
static int flag9118 = 0;

/*SyncMode*/
static int SyncMode =0;

static struct notifier_block hifi_fb_notifier;

struct workqueue_struct *hifi_workqueue;                                  
struct delayed_work hifi_delayed_work;  

static struct es9118_priv *g_es9118_priv = NULL;
static int es9118_write_reg(struct i2c_client *client, u8 reg, u8 value);
static u8 es9118_read_reg(struct i2c_client *client, u8 reg);

static const char const *Es9118_ByPass_Mode[] = {"Off", "On"};
static const char const *Es9118_HIFI_Mode[] = {"Off", "On"};

static const struct soc_enum Es9118_HIFI_Enum[] = {
	SOC_ENUM_SINGLE_EXT(ARRAY_SIZE(Es9118_HIFI_Mode), Es9118_HIFI_Mode),
};

static const struct soc_enum Es9118_ByPass_Enum[] = {
	SOC_ENUM_SINGLE_EXT(ARRAY_SIZE(Es9118_ByPass_Mode), Es9118_ByPass_Mode),
};


static int hifi_fb_notifier_callback(struct notifier_block *self, unsigned long event, void *data)
{
    struct fb_event *evdata = NULL;
    int blank;
    int err = 0;

    pr_aud_es9118("hifi_fb_notifier_callback\n");

    evdata = data;
    /* If we aren't interested in this event, skip it immediately ... */
    if (event != FB_EVENT_BLANK)
        return 0;

    blank = *(int *)evdata->data;
//    pr_aud_es9118("fb_notify(blank=%d)\n", blank);
    switch (blank) {
    case FB_BLANK_UNBLANK:
        pr_aud_es9118("haha-LCD ON Notify\n"); 
        if( 5== get_headset_status() )
        {   
        	if( 4 == get_9118_status() )
        	{
        		pr_aud_es9118("Call Mode Screen on!\n");
        	}else{
        	    pr_aud_es9118("Normal Mode headset has insert\n");           
        	    es9118_single_write();
        	}
        }
        break;
    case FB_BLANK_POWERDOWN:
        pr_aud_es9118("haha-LCD OFF Notify\n");
        break;
    default:
        break;
    }   
    return 0;

}


// Hifi->Standby->ByPass
int es9118_close(void)
{
	if(1 ==  get_9118_status())
	{
		// printk("Has been closed \n"); 
		return 0;
	}
	pr_aud_es9118("es-qqj-%s-%d\n",__func__,__LINE__);

	es9118_write_reg(g_es9118_priv->i2c_client, 14, 0x05);	
	es9118_write_reg(g_es9118_priv->i2c_client, 2, 0xB4);
	es9118_write_reg(g_es9118_priv->i2c_client, 5, 0x0);
	es9118_write_reg(g_es9118_priv->i2c_client, 4, 0xFF);
	es9118_write_reg(g_es9118_priv->i2c_client, 20, 0xFF);

	msleep(15);
	es9118_write_reg(g_es9118_priv->i2c_client, 32, 0x80);
	msleep(10); 
	//es9118_write_reg(g_es9118_priv->i2c_client, 29, 0x0);
	es9118_write_reg(g_es9118_priv->i2c_client, 46, 0x80); 

	msleep(15);

	AudDrv_Es9118_MODE_Select(2);   //rst->low

	AudDrv_Es9118_MODE_Select(4);   //Gpio2->low

	AudDrv_GPIO_OSC_Select(0);      //OSC->low 

	set_9118_status(1);

	msleep(150);  //Standby 100ms

	return 1; 
  
}

int es9118_single_write(void)
{
	unsigned char i,reg_value=0;
	pr_aud_es9118("es-qqj-%s-%d\n",__func__,__LINE__);

	if(3 ==  get_9118_status() )
	{
		  pr_aud_es9118("es-qqj-has enter Hifi Mode!\n");
		  return 0;
	}

	AudDrv_GPIO_OSC_Select(1);   //OSC->enable
	msleep(5);

	AudDrv_Es9118_MODE_Select(4);
	AudDrv_Es9118_MODE_Select(1);  //12-rst Standby->Hifi
	udelay(500);
	AudDrv_Es9118_MODE_Select(2);
	udelay(500);

	AudDrv_Es9118_MODE_Select(1);
	msleep(5);

	es9118_write_reg(g_es9118_priv->i2c_client, 0, 0x01);
	es9118_write_reg(g_es9118_priv->i2c_client, 1, 0x80);
	msleep(5);

	for( i=0; i<3; i++)
	{
		reg_value = es9118_read_reg(g_es9118_priv->i2c_client, 1);
		if( reg_value != 0x80)
		{
			AudDrv_Es9118_MODE_Select(2);
		    	udelay(500);

		    	AudDrv_Es9118_MODE_Select(1);
		    	msleep(5);

		    	es9118_write_reg(g_es9118_priv->i2c_client, 0, 0x01);
		    	es9118_write_reg(g_es9118_priv->i2c_client, 1, 0x80);
		    	msleep(5);
		}else 
		{
			pr_aud_es9118("es-qqj-try-%d-times\n",i);
			break;
		}
	}

	es9118_write_reg(g_es9118_priv->i2c_client, 14, 0x45);
	es9118_write_reg(g_es9118_priv->i2c_client, 2, 0xB4);
	es9118_write_reg(g_es9118_priv->i2c_client, 5, 0x0);
	es9118_write_reg(g_es9118_priv->i2c_client, 4, 0xFF);
	es9118_write_reg(g_es9118_priv->i2c_client, 32, 0x80);	
	msleep(5);
	es9118_write_reg(g_es9118_priv->i2c_client, 29, 0x0D);
	msleep(10);
	es9118_write_reg(g_es9118_priv->i2c_client, 46, 0x80);
	msleep(5);
	es9118_write_reg(g_es9118_priv->i2c_client, 32, 0x3);
	es9118_write_reg(g_es9118_priv->i2c_client, 12, 0xFA);

	es9118_write_reg(g_es9118_priv->i2c_client, 15, 0x03);
	es9118_write_reg(g_es9118_priv->i2c_client, 16, 0x03);
		
	queue_delayed_work(hifi_workqueue, &hifi_delayed_work, 10); 

	set_9118_status(3);

	msleep(5);

	return 0;
}

int es9118_hifi_init(void)
{
#if 0
	printk("es-qqj-%s-%d\n",__func__,__LINE__);

	es9118_write_reg(g_es9118_priv->i2c_client, 21, 0x08);
	es9118_write_reg(g_es9118_priv->i2c_client, 7, 0x40);

	es9118_write_reg(g_es9118_priv->i2c_client, 10, 0x0F);   
	es9118_write_reg(g_es9118_priv->i2c_client, 12, 0xFA);
	es9118_write_reg(g_es9118_priv->i2c_client, 6, 0x7); 

	es9118_write_reg(g_es9118_priv->i2c_client, 46, 0x0);
	es9118_write_reg(g_es9118_priv->i2c_client, 5, 0x7F);
	es9118_write_reg(g_es9118_priv->i2c_client, 4, 0x00);

	es9118_write_reg(g_es9118_priv->i2c_client, 15, 0x09);   //Volume for MP
	es9118_write_reg(g_es9118_priv->i2c_client, 16, 0x09);   //Volume for MP

	es9118_write_reg(g_es9118_priv->i2c_client, 13, 0x0);
	es9118_write_reg(g_es9118_priv->i2c_client, 22, 0xC8);  //THD+N
	es9118_write_reg(g_es9118_priv->i2c_client, 23, 0x0);   //THD+N
	es9118_write_reg(g_es9118_priv->i2c_client, 24, 0x7D);  //THD+N
	es9118_write_reg(g_es9118_priv->i2c_client, 25, 0xFF);  //THD+N 
#endif
    return 1;
}


void es9118_hifi_mode_init(void)
{
	pr_aud_es9118("es-qqj-%s-%d\n",__func__,__LINE__);

	es9118_write_reg(g_es9118_priv->i2c_client, 21, 0x08);
	es9118_write_reg(g_es9118_priv->i2c_client, 7, 0x40);

	es9118_write_reg(g_es9118_priv->i2c_client, 10, 0x0F);   
	es9118_write_reg(g_es9118_priv->i2c_client, 12, 0xFA);
	es9118_write_reg(g_es9118_priv->i2c_client, 6, 0x7); 

	es9118_write_reg(g_es9118_priv->i2c_client, 46, 0x0);
	es9118_write_reg(g_es9118_priv->i2c_client, 5, 0x7F);
	es9118_write_reg(g_es9118_priv->i2c_client, 4, 0x00);

	es9118_write_reg(g_es9118_priv->i2c_client, 15, 0x09);   //Volume for MP
	es9118_write_reg(g_es9118_priv->i2c_client, 16, 0x09);   //Volume for MP

	es9118_write_reg(g_es9118_priv->i2c_client, 13, 0x0);
	es9118_write_reg(g_es9118_priv->i2c_client, 22, 0xC8);  //THD+N
	es9118_write_reg(g_es9118_priv->i2c_client, 23, 0x0);   //THD+N
	es9118_write_reg(g_es9118_priv->i2c_client, 24, 0x7D);  //THD+N
	es9118_write_reg(g_es9118_priv->i2c_client, 25, 0xFF);  //THD+N 

}
int get_headset_status(void)
{
	pr_aud_es9118("fun-%s-status-%d\n",__func__,headset_status);
	return headset_status;
}

void set_headset_status(int status)
{
	pr_aud_es9118("fun-%s-status-%d\n",__func__,status);
	headset_status=status;
}

int get_9118_status(void)                                                                                                                                                                                
{
    pr_aud_es9118("fun-%s-status-%d\n",__func__,flag9118);
    return flag9118;
}

void set_9118_status(int status)     
{
    pr_aud_es9118("fun-%s-status-%d\n",__func__,status);
    flag9118 = status;
}

static int es9118_get_hifi_state_enum(struct snd_kcontrol *kcontrol,
		struct snd_ctl_elem_value *ucontrol)
{
	pr_aud_es9118("es-qqj-%s-%d\n",__func__,__LINE__);

	return 0;
}

static int es9118_put_hifi_state_enum(struct snd_kcontrol *kcontrol,
		struct snd_ctl_elem_value *ucontrol)
{
	int ret = 0;
//	printk("es-qqj-%s-%d\n",__func__,__LINE__);
	pr_aud_es9118("%s: ucontrol->value.integer.value[0]  = %ld\n",
		__func__, ucontrol->value.integer.value[0]);
    /*
	if (ucontrol->value.integer.value[0])
		ret = es9118_open();
	else
		ret = es9118_close();
	*/
	return ret;
}

static int es9118_get_i2s_length(struct snd_kcontrol *kcontrol,
		struct snd_ctl_elem_value *ucontrol)
{
	u8 reg_val;
//	printk("es-qqj-%s-%d\n",__func__,__LINE__);
	reg_val = es9118_read_reg(g_es9118_priv->i2c_client, INPUT_CONFIG_SOURCE);
	reg_val = reg_val >> 6;
	ucontrol->value.integer.value[0] = reg_val;

	pr_aud_es9118("%s: i2s_length = 0x%x\n", __func__, reg_val);

	return 0;
}

static int es9118_set_i2s_length(struct snd_kcontrol *kcontrol,
		struct snd_ctl_elem_value *ucontrol)
{
	u8 reg_val;
//	printk("es-qqj-%s-%d\n",__func__,__LINE__);
	pr_aud_es9118("%s: ucontrol->value.integer.value[0]  = %ld\n",
		__func__, ucontrol->value.integer.value[0]);

	reg_val = es9118_read_reg(g_es9118_priv->i2c_client, INPUT_CONFIG_SOURCE );

	reg_val &= ~(I2S_BIT_FORMAT_MASK);
	reg_val |=  ucontrol->value.integer.value[0] << 6;

	es9118_write_reg(g_es9118_priv->i2c_client,
				INPUT_CONFIG_SOURCE, reg_val);
	return 0;
}

static int es9118_bypass_get(struct snd_kcontrol *kcontrol,
			       struct snd_ctl_elem_value *ucontrol)
{
	pr_aud_es9118("es-qqj-%s-%d\n",__func__,__LINE__);

	return 0;
}

static int es9118_bypass_set(struct snd_kcontrol *kcontrol,
		struct snd_ctl_elem_value *ucontrol)
{
//	printk("es-qqj-%s-%d\n",__func__,__LINE__);
	if (ucontrol->value.integer.value[0] == 1) {
		AudDrv_Es9118_MODE_Select(2);
		AudDrv_Es9118_MODE_Select(3);
		pr_aud_es9118("%s enter ByPass, line %d\n", __func__, __LINE__);
	} else {
		pr_aud_es9118("%s exit ByPass, line %d\n", __func__, __LINE__);
		if( headset_status == 5 )
		{
			//es9118_single_write();
		}else{
		    AudDrv_Es9118_MODE_Select(2);
		    AudDrv_Es9118_MODE_Select(4); 	
		}
	}
	return 0;
}


static int es9118_hifi_get(struct snd_kcontrol *kcontrol,
			       struct snd_ctl_elem_value *ucontrol)
{
	pr_aud_es9118("es-qqj-%s-%d\n",__func__,__LINE__);

	return 0;
}

static int es9118_hifi_set(struct snd_kcontrol *kcontrol,
		struct snd_ctl_elem_value *ucontrol)
{
//	printk("es-qqj-%s-%d\n",__func__,__LINE__);
	if (ucontrol->value.integer.value[0] == 1) {
		//es9118_single_write();
		pr_aud_es9118("%s enter Hifi Mode, line %d\n", __func__, __LINE__);
	} else {
		//es9118_close();
		pr_aud_es9118("%s quit ByPass Mode , line %d\n", __func__, __LINE__);
	}
	return 0;
}


static const char * const es9118_hifi_state_texts[] = {
	"Off", "On"
};

static const char * const es9118_i2s_length_texts[] = {
	"16bit", "24bit", "32bit", "32bit"
};

static const char * const es9118_clk_divider_texts[] = {
	"DIV4", "DIV8", "DIV16", "DIV16"
};

static const struct soc_enum es9118_hifi_state_enum =
SOC_ENUM_SINGLE_EXT(ARRAY_SIZE(es9118_hifi_state_texts),
		es9118_hifi_state_texts);

static const struct soc_enum es9118_i2s_length_enum =
SOC_ENUM_SINGLE_EXT(ARRAY_SIZE(es9118_i2s_length_texts),
		es9118_i2s_length_texts);

static const struct soc_enum es9118_clk_divider_enum =
SOC_ENUM_SINGLE_EXT(ARRAY_SIZE(es9118_clk_divider_texts),
		es9118_clk_divider_texts);

static struct snd_kcontrol_new es9118_digital_ext_snd_controls[] = {
	/* commit controls */
	SOC_ENUM_EXT("Es9118 HIFI State", es9118_hifi_state_enum,
			es9118_get_hifi_state_enum, es9118_put_hifi_state_enum),

	SOC_ENUM_EXT("Es9118 I2s Length", es9118_i2s_length_enum,
			es9118_get_i2s_length, es9118_set_i2s_length),
			
	SOC_ENUM_EXT("Es9118_Bypass_Switch", Es9118_ByPass_Enum[0],
		es9118_bypass_get, es9118_bypass_set),	
		
	SOC_ENUM_EXT("Es9118_HIFI_Switch", Es9118_HIFI_Enum[0],
		es9118_hifi_get, es9118_hifi_set),	
			
};

//-1 read fail,others is fine
static u8 es9118_read_reg(struct i2c_client *client, u8 reg)
{
	int ret = 0;
	u8 data = 0;

	// write reg addr     
	ret = i2c_master_send(client, &reg, 1);
	if( ret == 1 ) {  
	    msleep(10);  
	 //   printk("es9118-qqj-write-ok\n");
	    ret = i2c_master_recv(client, &data, 1);
	    if( ret == 1) {  
	        pr_aud_es9118("es9118_read_reg=%d,data=%d\n",reg,data);
	        return data;
	    }      
	} else {
		 printk( KERN_ERR " es9118_read_reg write fail! \n" );  
	     return -1; 
	}     

	return -1;
}

//-1 write fail,others is fine
static int es9118_write_reg(struct i2c_client *client, u8 reg, u8 value)
{
	int ret;
	char write_data[2] = { 0 };
	
	mutex_lock(&es9118c_access);

	write_data[0] = reg;
	write_data[1] = value;

	ret = i2c_master_send(client, write_data, 2);
	if (ret < 0) {
		mutex_unlock(&es9118c_access);
		printk("qqj-9118-i2c_master_send  write error\n");
		return -1;
	}

	mutex_unlock(&es9118c_access);
	
	return ret;
}

/*for chip SelfCheck*/
static ssize_t es9118_self_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	u8 i=0;
	char ret='0';
	u8 reg_value[34]={0,0};
	pr_aud_es9118("ess-qqj-%s-headset_status=%d\n",__func__,headset_status);

	//SelfCheck Init

	AudDrv_GPIO_OSC_Select(1);
	msleep(5);

	AudDrv_Es9118_MODE_Select(1);  //12-rst Standby->Hifi
	udelay(500);

	AudDrv_Es9118_MODE_Select(2);
	udelay(500);

	AudDrv_Es9118_MODE_Select(1);
	msleep(100);

	//Chip SelfCheck
	for(i = 0; i < 34; i++) {
		reg_value[i] = es9118_read_reg(g_es9118_priv->i2c_client, i);
		if(reg_value[i] == reg_default[i])  //SelfCheck Success
		{	
			ret='1';
			pr_aud_es9118("ess-qqj-value-Success=%d\n",reg_value[i]);

		}else   //SelfCheck failed
		{
			ret='0';
			pr_aud_es9118("ess-qqj-value-failed=%d\n",reg_value[i]);
			break;
		}	
	}

	//Site Recovery
	AudDrv_Es9118_MODE_Select(2);
	AudDrv_GPIO_OSC_Select(0);
	//es9118_close();
	AudDrv_GPIO_OSC_Select(0);

	//return Values
	*buf = ret;

	return 1;
}

static ssize_t es9118_self_store(struct device *dev,struct device_attribute *attr,const char *buf, size_t count)
{

	pr_aud_es9118("es9118_self_store%s\n",__func__);
	   
	return count;   
}

/*Show Chip Reg Value*/
static ssize_t es9118_value_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	u8 i,ret=0;
	int len=0;
	u8 reg_value[48]={0,0};
	pr_aud_es9118("ess-qqj-%s-headset_status=%d\n",__func__,headset_status);

	for(i = 0; i < 44; i++) {
		reg_value[i] = es9118_read_reg(g_es9118_priv->i2c_client, i);
		len += snprintf(buf+len, 36, "reg:0x%04X value: 0x%04X\n", i, reg_value[i]);
		pr_aud_es9118("ess-qqj-value=%d\n",reg_value[i]); 
	}
	pr_aud_es9118("ess-qqj-%s\n",buf);
	
    return len;
}

/*Store Chip Reg Value*/
static ssize_t es9118_value_store(struct device *dev,struct device_attribute *attr,const char *buf, size_t count)
{
	int ret = 0;
	u8 reg,value;
	pr_aud_es9118("ess-qqj-%s-headset_status=%d\n",__func__,headset_status);

	ret = sscanf(buf, "%d %d", &reg, &value);
//	printk("haha-mx%d\n",ret);
		
	if(ret == 2)
	{
	//	printk("es9118-qqj-reg-%d--val-%d!\n",reg,value);
       		
      	ret=es9118_write_reg(g_es9118_priv->i2c_client, reg, value);
       	if (ret)
	      	return count;
	}else 
	  	return -1;
	   
	return count;   
}

static DEVICE_ATTR(es9118_value, S_IRUGO|S_IWUSR, es9118_value_show, es9118_value_store);

static DEVICE_ATTR(es9118_self, S_IRUGO|S_IWUSR, es9118_self_show, es9118_self_store);

static int es9118_pcm_hw_params(struct snd_pcm_substream *substream,
		struct snd_pcm_hw_params *params,
		struct snd_soc_dai *codec_dai)
{
	/* struct snd_soc_codec *codec = codec_dai->codec; */
	/* struct es9118_priv *priv = codec->control_data; */
  //  printk("es-qqj-%s-%d\n",__func__,__LINE__);
	return 0;
}

static int es9118_mute(struct snd_soc_dai *dai, int mute)
{
	/* struct snd_soc_codec *codec = codec_dai->codec; */
	/* struct es9118_priv *priv = codec->control_data; */
  //  printk("es-qqj-%s-%d\n",__func__,__LINE__);
	return 0;
}

static int es9118_set_clkdiv(struct snd_soc_dai *codec_dai,
				int div_id, int div)
{
	/* struct snd_soc_codec *codec = codec_dai->codec; */
	/* struct es9118_priv *priv = codec->control_data; */
 //   printk("es-qqj-%s-%d\n",__func__,__LINE__);
	return 0;
}

static int es9118_set_dai_sysclk(struct snd_soc_dai *codec_dai,
		int clk_id, unsigned int freq, int dir)
{
	/* struct snd_soc_codec *codec = codec_dai->codec; */
	/* struct es9118_priv *priv = codec->control_data; */
//    printk("es-qqj-%s-%d\n",__func__,__LINE__);
	return 0;
}


static int es9118_set_dai_fmt(struct snd_soc_dai *codec_dai,
				unsigned int fmt)
{
	/* struct snd_soc_codec *codec = codec_dai->codec; */
	/* struct es9118_priv *priv = codec->control_data; */
  //  printk("es-qqj-%s-%d\n",__func__,__LINE__);
	return 0;
}

static int es9118_set_fll(struct snd_soc_dai *codec_dai,
		int pll_id, int source, unsigned int freq_in,
		unsigned int freq_out)
{
	/* struct snd_soc_codec *codec = codec_dai->codec; */
	/* struct es9118_priv *priv = codec->control_data; */
//    printk("es-qqj-%s-%d\n",__func__,__LINE__);
	return 0;
}

static int es9118_pcm_trigger(struct snd_pcm_substream *substream,
		int cmd, struct snd_soc_dai *codec_dai)
{
	/* struct snd_soc_codec *codec = codec_dai->codec; */
	/* struct es9118_priv *priv = codec->control_data; */
//    printk("es-qqj-%s-%d\n",__func__,__LINE__);
	return 0;
}

static const struct snd_soc_dai_ops es9118_dai_ops = {
	.hw_params	= es9118_pcm_hw_params,
	.digital_mute	= es9118_mute,
	.trigger	= es9118_pcm_trigger,
	.set_fmt	= es9118_set_dai_fmt,
	.set_sysclk	= es9118_set_dai_sysclk,
	.set_pll	= es9118_set_fll,
	.set_clkdiv	= es9118_set_clkdiv,
};

static struct snd_soc_dai_driver es9118_dai = {
	.name = "es9118-hifi",
	.playback = {
		.stream_name = "I2S0_PLayback", //"Playback",
		.channels_min = 1,
		.channels_max = 8,
		.rates = SNDRV_PCM_RATE_8000_192000,//ES9118_RATES,
		.formats =  (SNDRV_PCM_FMTBIT_U8 | SNDRV_PCM_FMTBIT_S8 |
				 SNDRV_PCM_FMTBIT_U16_LE | SNDRV_PCM_FMTBIT_S16_LE |
				 SNDRV_PCM_FMTBIT_U16_BE | SNDRV_PCM_FMTBIT_S16_BE |
				 SNDRV_PCM_FMTBIT_U24_LE | SNDRV_PCM_FMTBIT_S24_LE |
				 SNDRV_PCM_FMTBIT_U24_BE | SNDRV_PCM_FMTBIT_S24_BE |
				 SNDRV_PCM_FMTBIT_U24_3LE | SNDRV_PCM_FMTBIT_S24_3LE |
				 SNDRV_PCM_FMTBIT_U24_3BE | SNDRV_PCM_FMTBIT_S24_3BE |
				 SNDRV_PCM_FMTBIT_U32_LE | SNDRV_PCM_FMTBIT_S32_LE |
				 SNDRV_PCM_FMTBIT_U32_BE | SNDRV_PCM_FMTBIT_S32_BE),//ES9118_FORMATS,
	},
	.capture = {
	    .stream_name = "I2S0_PLayback",
		.channels_min = 1,
		.channels_max = 8,
		 .rates = SNDRV_PCM_RATE_8000_192000,//ES9118_RATES,
		.formats =  (SNDRV_PCM_FMTBIT_U8 | SNDRV_PCM_FMTBIT_S8 |
				 SNDRV_PCM_FMTBIT_U16_LE | SNDRV_PCM_FMTBIT_S16_LE |
				 SNDRV_PCM_FMTBIT_U16_BE | SNDRV_PCM_FMTBIT_S16_BE |
				 SNDRV_PCM_FMTBIT_U24_LE | SNDRV_PCM_FMTBIT_S24_LE |
				 SNDRV_PCM_FMTBIT_U24_BE | SNDRV_PCM_FMTBIT_S24_BE |
				 SNDRV_PCM_FMTBIT_U24_3LE | SNDRV_PCM_FMTBIT_S24_3LE |
				 SNDRV_PCM_FMTBIT_U24_3BE | SNDRV_PCM_FMTBIT_S24_3BE |
				 SNDRV_PCM_FMTBIT_U32_LE | SNDRV_PCM_FMTBIT_S32_LE |
				 SNDRV_PCM_FMTBIT_U32_BE | SNDRV_PCM_FMTBIT_S32_BE),
	},
	.ops = &es9118_dai_ops,
};

static  int es9118_codec_probe(struct snd_soc_codec *codec)
{
	int rc = 0;
	
//	printk("es-qqj-%s-%d\n",__func__,__LINE__);

	rc = snd_soc_add_codec_controls(codec, es9118_digital_ext_snd_controls,
			ARRAY_SIZE(es9118_digital_ext_snd_controls));
	if (rc)
		dev_err(codec->dev, "%s(): es325_digital_snd_controls failed\n",
			__func__);

	return 0;
}

static int  es9118_codec_remove(struct snd_soc_codec *codec)
{
//	printk("es-qqj-%s-%d\n",__func__,__LINE__);
	
	//AudDrv_GPIO_Power_Select(0);

	return 0;
}

static int es9118_suspend(struct snd_soc_codec *codec)
{
//	printk("es-qqj-%s-%d\n",__func__,__LINE__);
	
	if( 4 == get_9118_status())  //In Call 
	{
	  //  printk("es-qqj-In-Call-%d\n",__LINE__);
	}else   //Not In Call
	{
	  //  printk("es-qqj-Not-In-Call-%d\n",__LINE__);
	    if(headset_status==5)  //headset plug in
		{
		   es9118_close();
		}
	}
	return 0;
}

static int es9118_resume(struct snd_soc_codec *codec)
{
//	printk("es-qqj-=%s-%d\n",__func__,__LINE__);

	if( 4 == get_9118_status())  //In Call 
	{
//	    printk("es-qqj-In-Call-%d\n",__LINE__);
	}else   //Not In Call
	{
		if( headset_status==5 )  //headset plug in
		{
//			printk("es-qqj-%s-%d\n",__func__,__LINE__);
		}else 
		{
//	    	printk("es-qqj-%s-%d\n",__func__,__LINE__);
		}
    }
    
	return 0;
}

static struct snd_soc_codec_driver soc_codec_dev_es9118 = {
	.probe   = es9118_codec_probe,
	.remove  = es9118_codec_remove,
	
};

static int es9118_probe(struct i2c_client *client,const struct i2c_device_id *id)
{
	int ret = 0;
	struct es9118_priv *priv;
	// register kcontrol
	 
//	printk("qqj-open-start-RET=%s!!\n",__func__);

	if (!i2c_check_functionality(client->adapter,
				I2C_FUNC_SMBUS_BYTE_DATA)) {
		dev_err(&client->dev, "%s: no support for i2c read/write"
				"byte data\n", __func__);
		return -EIO;
	}

	if (!i2c_check_functionality(client->adapter,
				I2C_FUNC_SMBUS_BYTE_DATA)) {
		dev_err(&client->dev, "%s: no support for i2c read/write"
				"byte data\n", __func__);
		return -EIO;
	}

	priv = devm_kzalloc(&client->dev, sizeof(struct es9118_priv),
			GFP_KERNEL);
	if (priv == NULL)
		return -ENOMEM;
	priv->i2c_client = client;
	i2c_set_clientdata(client, priv);

	g_es9118_priv = priv;

	hifi_workqueue = create_singlethread_workqueue("es9118_hifi_init_queue");
	INIT_DELAYED_WORK(&hifi_delayed_work, es9118_hifi_init); 

	AudDrv_GPIO_Power_Select(1);

	msleep(10);

	/*check-accdet-insert*/
	if( 5== get_headset_status() ){     
		es9118_single_write();
	//	printk("kkkk-kaijiyouerji");     
	}else {  
		AudDrv_Es9118_MODE_Select(2);
		AudDrv_GPIO_OSC_Select(0);
	//	printk("xxxx-kaijimeierji");	 
	}

	/**/
	hifi_fb_notifier.notifier_call = hifi_fb_notifier_callback;                             
	if (fb_register_client(&hifi_fb_notifier))
	    printk("register fb_notifier fail!\n");


	flag9118 =1; /*9118-StartUp flag*/

//	printk("qqj-open-finished-RET=%d!!\n",ret);

	return ret;
}

static int es9118_remove(struct i2c_client *client)
{
//	printk("es-qqj-%s-%d\n",__func__,__LINE__);

	return 0;
}

static const struct dev_pm_ops es9118_pm_ops = {
	.suspend = es9118_suspend,
	.resume  = es9118_resume,                                                                                                                               
};  

static struct i2c_driver es9118_i2c_driver = {
	.driver	= {
		.owner  = THIS_MODULE,
		.name	= ES9118_I2C_NAME,
		.pm    = &es9118_pm_ops,
		.of_match_table = es9118_match_table,
	},
	.probe		= es9118_probe,
	.remove		= es9118_remove,
	
	.id_table   = es9118_i2c_id,
};


static int mtk_dac_9118_dev_probe(struct platform_device *pdev)
{

	if (pdev->dev.of_node)
		dev_set_name(&pdev->dev, "%s", MT_SOC_DACES_NAME);

//	pr_warn("%s: dev name %s\n", __func__, dev_name(&pdev->dev));
	
	return snd_soc_register_codec(&pdev->dev, &soc_codec_dev_es9118, &es9118_dai, 1 );
}

static int mtk_dac_9118_dev_remove(struct platform_device *pdev)
{
//	pr_warn("%s:\n", __func__);

	snd_soc_unregister_codec(&pdev->dev);
	return 0;

}


static struct platform_driver mtk_dac_9118_driver = {
	.driver = {
		   .name = MT_SOC_DACES_NAME,
		   .owner = THIS_MODULE,
		   },
	.probe = mtk_dac_9118_dev_probe,
	.remove = mtk_dac_9118_dev_remove,
};

static struct platform_device *soc_mtk_dac9118_dev;

static int __init es9118c_init(void)
{
    int ret;
  
    ret = i2c_add_driver(&es9118_i2c_driver);
    if (ret != 0) {
//		printk("[%s] failed to register es9118 i2c driver.\n", __func__);
		return ret;
	} else {
//		printk("[%s] Success to register es9118 i2c driver.\n", __func__);
	}
//	printk("es-qqj-%s-%d ret = %d\n",__func__,__LINE__,ret);
	
	debug_kobj = kobject_create_and_add("es9118", NULL) ;
    if (debug_kobj == NULL)
    {
        printk("%s: subsystem_register failed\n", __func__);
        return -ENOMEM;
    }
 
    ret = sysfs_create_file(debug_kobj, &dev_attr_es9118_value.attr);
    if (ret)
    {
//        printk("%s: sysfs_create_es9118_value_file failed\n", __func__);
        return ret;
    }
    
    ret = sysfs_create_file(debug_kobj, &dev_attr_es9118_self.attr);
    if (ret)
    {
 //       printk("%s: sysfs_create_es9118_self_file failed\n", __func__);
        return ret;
    }
	
	soc_mtk_dac9118_dev = platform_device_alloc(MT_SOC_DACES_NAME, -1);

	if (!soc_mtk_dac9118_dev)
		return -ENOMEM;

	ret = platform_device_add(soc_mtk_dac9118_dev);
	if (ret != 0) {
		platform_device_put(soc_mtk_dac9118_dev);
		return ret;
	}
	
	ret = platform_driver_register(&mtk_dac_9118_driver);
//         printk("mtk_dac_9118_driver register result:%d \n", ret);
	if (ret != 0) {
		platform_driver_unregister(&mtk_dac_9118_driver);
	}
	
    return ret;
}

static void __exit es9118c_exit(void)
{
//	printk("es-qqj-%s-%d\n",__func__,__LINE__);
	platform_driver_unregister(&mtk_dac_9118_driver);
	i2c_del_driver(&es9118_i2c_driver);
}

module_init(es9118c_init);
module_exit(es9118c_exit);

MODULE_DESCRIPTION("ASoC ES9118 driver");
MODULE_LICENSE("GPL");
MODULE_ALIAS("platform:es9118-codec");
