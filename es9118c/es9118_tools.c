/* Copyright (c) 2011-2014, The Linux Foundation. All rights reserved.
   by Meitu Driver QQJ
 */
 

 /*工具文件，实现或封装iic读写，gpio的设定，reset功能，iis的通路设定
   
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/of_gpio.h>
#include <linux/of_irq.h>
#include <linux/slab.h>
#include <linux/ratelimit.h>
#include <linux/mfd/core.h>

#include <linux/spi/spi.h>
#include <linux/delay.h>
#include <linux/gpio.h>
#include <linux/debugfs.h>
#include <linux/regulator/consumer.h>
#include <sound/soc.h>
#include <linux/regmap.h>

#include "es9118c.h"

struct pinctrl *es9118_pinctrl;

enum audio_ext_codec_type {
	ESS_RST_HIGH,
	ESS_RST_LOW,
	ESS_MODE_HIGH,
	ESS_MODE_LOW,
	ESS_ENABLE_HIGH,
	ESS_DISABLE_LOW,
	HP_SWITCH_HIGH,
	HP_SWITCH_LOW,
	ESS_OSC_HIGH,
	ESS_OSC_LOW,
	GPIO_NUM
};

struct es9118_gpio_attr {
	const char *name;
	bool gpio_prepare;
	struct pinctrl_state *gpioctrl;
};

static struct es9118_gpio_attr es9118_gpios[GPIO_NUM] = {
	[ESS_MODE_HIGH] = {"ess_mode_pin_high", false, NULL},
	[ESS_MODE_LOW] = {"ess_mode_pin_low", false, NULL},
	[ESS_RST_HIGH] = {"ess_reset_pin_high", false, NULL},
	[ESS_RST_LOW] = {"ess_reset_pin_low", false, NULL},
	[ESS_ENABLE_HIGH] = {"ess_power_enable_high", false, NULL},
	[ESS_DISABLE_LOW] = {"ess_power_disable_low", false, NULL},
	[HP_SWITCH_HIGH] = {"headset_switch_high", false, NULL},
	[HP_SWITCH_LOW] = {"headset_switch_low", false, NULL},
	[ESS_OSC_HIGH] = {"ess_osc_enable_high", false, NULL},
	[ESS_OSC_LOW] = {"ess_osc_disable_low", false, NULL},
};

/*
  默认无耳机待机状态即为Standby，Standby（Workstatus=1）Normal（Workstatus=2）Bypass（Workstatus=3）
  未初始化时默认值（Workstatus=0）*/
  
int AudDrv_Es9118_MODE_Select(int mode)
{   
	int retval = 0;   
	pr_aud_es9118("es9118:--%s-mode-%d \n",__func__,mode);
	
	switch(mode){
		
	    case 1:	//Rst-high
	    	if (es9118_gpios[ESS_RST_HIGH].gpio_prepare) {   //rst pull high
				retval = pinctrl_select_state(es9118_pinctrl, es9118_gpios[ESS_RST_HIGH].gpioctrl);
				if (retval)
					pr_err("could not set es9118_gpios[ESS_RST_HIGH] pins\n");	
	    	}    
	    	break;
	    
	    case 2:  //Rst-low
			if (es9118_gpios[ESS_RST_LOW].gpio_prepare) {   //rst pull high
				retval = pinctrl_select_state(es9118_pinctrl, es9118_gpios[ESS_RST_LOW].gpioctrl);
				if (retval)
					pr_err("could not set es9118_gpios[ESS_RST_LOW] pins\n");	
	    	}    
	    	break;
	    
	    case 3: //mode-high    	
    		if (es9118_gpios[ESS_MODE_HIGH].gpio_prepare) {   //gpio2 pull up
				retval = pinctrl_select_state(es9118_pinctrl, es9118_gpios[ESS_MODE_HIGH].gpioctrl);
				if (retval)
					pr_err("could not set es9118_gpios[ESS_MODE_HIGH] pins\n");
	    	}
	    	break;
	    		
	    case 4: //mode-low 
    		if (es9118_gpios[ESS_MODE_LOW].gpio_prepare) {   //gpio2 pull up
				retval = pinctrl_select_state(es9118_pinctrl, es9118_gpios[ESS_MODE_LOW].gpioctrl);
				if (retval)
					pr_err("could not set es9118_gpios[ESS_MODE_LOW] pins\n");
	    	}
	        break;
	    	  	
	    default:
	    	printk("Mode Select Error!\n");
			break;
	}
      
	return retval;
}

int AudDrv_GPIO_Power_Select(int bEnable)
{
	int retval = 0;
	pr_aud_es9118("qqj-ess9118-%s-enable-%d\n", __func__, bEnable);
	if (bEnable == 1) {
		if (es9118_gpios[ESS_ENABLE_HIGH].gpio_prepare) {
			retval = pinctrl_select_state(es9118_pinctrl, es9118_gpios[ESS_ENABLE_HIGH].gpioctrl);
			if (retval)
				pr_err("could not set es9118_gpios[ESS_ENABLE_HIGH] pins\n");
		}
	} else {
		if (es9118_gpios[ESS_DISABLE_LOW].gpio_prepare) {
			retval = pinctrl_select_state(es9118_pinctrl, es9118_gpios[ESS_DISABLE_LOW].gpioctrl);
			printk("ess9118-powerdown\n");
			if (retval)
				pr_err("could not set es9118_gpios[ESS_DISABLE_LOW] pins\n");
		}
	}
	return retval;
}


int AudDrv_GPIO_OSC_Select(int bEnable)
{
        int retval = 0;
        pr_aud_es9118("qqj-ess9118-%s-enable-%d\n", __func__, bEnable);
        if (bEnable == 1) {
                if (es9118_gpios[ESS_OSC_HIGH].gpio_prepare) {
                        retval = pinctrl_select_state(es9118_pinctrl, es9118_gpios[ESS_OSC_HIGH].gpioctrl);
                        if (retval)
                                pr_err("could not set es9118_gpios[ESS_OSC_HIGH] pins\n");
                }
        } else {
                if (es9118_gpios[ESS_OSC_LOW].gpio_prepare) {
                        retval = pinctrl_select_state(es9118_pinctrl, es9118_gpios[ESS_OSC_LOW].gpioctrl);
                        printk("ess9118-powerdown\n");
                        if (retval)
                                pr_err("could not set es9118_gpios[ESS_OSC_LOW] pins\n");
                }
        }
        return retval;
}


int AudDrv_HeadSet_Switch(int bEnable)
{
	int retval = 0;
	pr_aud_es9118("es9118:AudDrv_GPIO_ESS-bEnable %d\n",bEnable);

	if(bEnable)
	{
		if (es9118_gpios[HP_SWITCH_HIGH].gpio_prepare) {
			retval = pinctrl_select_state(es9118_pinctrl, es9118_gpios[HP_SWITCH_HIGH].gpioctrl);
			if (retval)
				pr_err("could not set es9118_gpios[HP_SWITCH_HIGH] pins\n");
		}
	        mdelay(1);
	}else{
		if (es9118_gpios[HP_SWITCH_LOW].gpio_prepare) {
			retval = pinctrl_select_state(es9118_pinctrl, es9118_gpios[HP_SWITCH_LOW].gpioctrl);
			if (retval)
				pr_err("could not set es9118_gpios[HP_SWITCH_LOW] pins\n");
		}
	}

	return retval;
}


static int es9118_gpio_probe(struct platform_device *pdev)
{
	int ret=0;
	 //
	int i = 0;

	pr_aud_es9118("es9118:%s\n", __func__);

	es9118_pinctrl = devm_pinctrl_get(&pdev->dev);
	if (IS_ERR(es9118_pinctrl)) {
		ret = PTR_ERR(es9118_pinctrl);
		pr_err("Cannot find es9118_pinctrl!\n");
		return ret;
	}

	for (i = 0; i < ARRAY_SIZE(es9118_gpios); i++) {
		es9118_gpios[i].gpioctrl = pinctrl_lookup_state(es9118_pinctrl, es9118_gpios[i].name);
		if (IS_ERR(es9118_gpios[i].gpioctrl)) {
			ret = PTR_ERR(es9118_gpios[i].gpioctrl);
			pr_err("%s pinctrl_lookup_state %s fail %d\n", __func__, es9118_gpios[i].name,
			       ret);
		} else {
			es9118_gpios[i].gpio_prepare = true;
		}
	}
    return ret;
}

static int es9118_gpio_remove(struct platform_device *pdev)
{
   
	return 0;
}

static const struct of_device_id es9118_of_match[] =
{
    { .compatible = "meitu,dac_es9118" },
    {},
};

static struct platform_driver es9118_gpio_driver = {
	.probe = es9118_gpio_probe,
	.remove = es9118_gpio_remove,
	.driver = {
		.name = "es9118_codec_gpio",
		.owner = THIS_MODULE,
		.of_match_table = es9118_of_match,
	},
};


//static int __init es9118_init(void)
static int  es9118_tool_init(void)
{
	int rtn;
	pr_aud_es9118("es9118_init\n");

	rtn = platform_driver_register(&es9118_gpio_driver);
	pr_aud_es9118("qqj-es9118_gpio_driver register result:%d \n", rtn);

	if (rtn != 0) {
		platform_driver_unregister(&es9118_gpio_driver);
	            return -1;
	}

	  return 0;
}
module_init(es9118_tool_init);

static void __exit es9118_tool_exit(void)
{
//	printk("es9118_exit\n");

	platform_driver_unregister(&es9118_gpio_driver);
	
}
module_exit(es9118_tool_exit);

MODULE_DESCRIPTION("es9118 gpio driver");
MODULE_VERSION("1.0");
MODULE_LICENSE("GPL v2");

