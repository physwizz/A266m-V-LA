// SPDX-License-Identifier: GPL-2.0-or-later
/*
 *  Sound Card Driver for Exynos8825
 *
 *  Copyright 2020 Samsung Electronics Co. Ltd.
 *
 *  This program is free software; you can redistribute  it and/or modify it
 *  under  the terms of  the GNU General  Public License as published by the
 *  Free Software Foundation;  either version 2 of the  License, or (at your
 *  option) any later version.
 */

#include <linux/clk.h>
#include <linux/regulator/consumer.h>
#include <linux/debugfs.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/clk.h>
#include <linux/slab.h>
#include <linux/pm_wakeup.h>

#include <sound/pcm_params.h>
#include <sound/soc.h>
#include <sound/samsung/abox.h>

#if IS_ENABLED(CONFIG_SND_SOC_TFA9878)
#include "../codecs/tfa9878/bigdata_tfa_sysfs_cb.h"
#endif
#if IS_ENABLED(CONFIG_SND_SMARTPA_AW882XX)
#include "../codecs/aw882xx/aw882xx.h"
#include "../codecs/aw882xx/bigdata_aw882xx_sysfs_cb.h"
#endif

#if IS_ENABLED(CONFIG_SEC_ABC)
#include <linux/sti/abc_common.h>
#endif

#if IS_ENABLED(CONFIG_SND_SOC_SAMSUNG_AUDIO)
#include <sound/samsung/sec_audio_sysfs.h>
#include <sound/samsung/snd_debug_proc.h>
#endif

#define ABOX_UAIF_DAI_ID(c, i)		(0xaf00 | (c) << 4 | (i))
#define ABOX_BE_DAI_ID(c, i)		(0xbe00 | (c) << 4 | (i))
#define CODEC_CONF_MAX			32
#define AUX_DEV_MAX			2
#define RDMA_COUNT			12
#define WDMA_COUNT			12
#if IS_ENABLED(CONFIG_SND_SOC_SAMSUNG_VTS)
#define VTS_COUNT			2
#else
#define VTS_COUNT			0
#endif
#if IS_ENABLED(CONFIG_SND_SOC_SAMSUNG_DISPLAYPORT)
#define DP_COUNT			2
#else
#define DP_COUNT			0
#endif
#define DDMA_COUNT			6
#define DUAL_COUNT			WDMA_COUNT
#define UAIF_START			(RDMA_COUNT + WDMA_COUNT + VTS_COUNT\
					+ DP_COUNT + DDMA_COUNT + DUAL_COUNT)
#define UAIF_COUNT			7

#define for_each_link_cpus(link, i, cpu)	\
	for ((i) = 0;				\
	     ((i) < link->num_cpus) &&		\
	     ((cpu) = &link->cpus[i]);		\
	     (i)++)

#define SUPPORT_AMP_READY_CALLBACK	0

struct _drvdata {
	struct device *dev;
	struct wakeup_source *ws;
};

static struct _drvdata exynos_drvdata;

static const struct snd_soc_ops rdma_ops = {
};

static const struct snd_soc_ops wdma_ops = {
};

#if IS_ENABLED(CONFIG_SND_SOC_SAMSUNG_AUDIO) && SUPPORT_AMP_READY_CALLBACK
static int get_audio_amp_ready(enum amp_id id)
{
	struct sound_drvdata *drvdata = &exynos_drvdata;
	int ret = NOT_SUPPORT;

	/* Implement codes for amp */
	dev_info(drvdata->dev, "%s: id %d value %d\n", __func__, id, ret);

	return ret;
}
#endif

#if IS_ENABLED(CONFIG_SND_SMARTPA_AW882XX)
extern void aw882xx_register_i2c_error_callback(int channel, void (*func)(int));

void aw882xx_i2c_fail_callback(int channel)
{
	pr_info("%s channel: 0x%02x\n", __func__, channel);
#if IS_ENABLED(CONFIG_SND_SOC_SAMSUNG_AUDIO)
	send_amp_i2c_fail_ev(channel);
#endif
#if IS_ENABLED(CONFIG_SEC_ABC)
#if IS_ENABLED(CONFIG_SEC_FACTORY)
	sec_abc_send_event("MODULE=audio@INFO=spk_amp");
#else
	sec_abc_send_event("MODULE=audio@WARN=spk_amp");
#endif
#endif
}

extern void aw882xx_register_short_circuit_callback(int channel, void (*func)(int));

void aw882xx_short_circuit_callback(int channel)
{
	pr_info("%s channel: 0x%02x\n", __func__, channel);
#if IS_ENABLED(CONFIG_SEC_ABC)
#if IS_ENABLED(CONFIG_SEC_FACTORY)
	sec_abc_send_event("MODULE=audio@INFO=spk_amp_short");
#else
	sec_abc_send_event("MODULE=audio@WARN=spk_amp_short");
#endif
#endif
}
#endif

static int uaif0_init(struct snd_soc_pcm_runtime *rtd)
{
	return 0;
}

static int uaif1_init(struct snd_soc_pcm_runtime *rtd)
{
	struct snd_soc_card *card = rtd->card;
	struct snd_soc_dai *dai;
	struct snd_soc_component *component = NULL;
	unsigned int num_codecs = rtd->dai_link->num_codecs;
	unsigned int i;
#if IS_ENABLED(CONFIG_SND_SMARTPA_AW882XX)
	struct snd_soc_dapm_context *dapm;
	unsigned int suffix;
	char buff[80];
	const int BUFF_SZ = 80;
	struct aw882xx *drvdata;
#endif

	dev_info(card->dev, "%s: num_codecs(%d)\n", __func__, num_codecs);

	for_each_rtd_codec_dais(rtd, i, dai) {
		component = dai->component;
		if (!component)
			continue;

		dev_info(card->dev, "%s: component[%d]->name=%s\n", __func__, i, component->name);
#if IS_ENABLED(CONFIG_SND_SOC_TFA9878)
		if ((component) && strstr(component->name, "tfa98xx"))
			register_tfa98xx_bigdata_cb(component);
#endif
#if IS_ENABLED(CONFIG_SND_SMARTPA_AW882XX)
		if (strstr(component->name, "aw882xx_smartpa")) {
			drvdata = (struct aw882xx *)snd_soc_component_get_drvdata(component);
			suffix = drvdata->aw_pa->channel;
			dapm = snd_soc_component_get_dapm(component);

			dev_info(card->dev, "%s: component(%s) suffix(%d)\n",
				__func__, component->name, suffix);

			register_aw882xx_bigdata_cb(component);

			snprintf(buff, BUFF_SZ, "%s_%d", "Speaker_Playback", suffix);
			snd_soc_dapm_ignore_suspend(dapm, buff);

			snprintf(buff, BUFF_SZ, "%s_%d", "Speaker_Capture", suffix);
			snd_soc_dapm_ignore_suspend(dapm, buff);

			snprintf(buff, BUFF_SZ, "%s_%d", "audio_out", suffix);
			snd_soc_dapm_ignore_suspend(dapm, buff);

			snprintf(buff, BUFF_SZ, "%s_%d", "iv_in", suffix);
			snd_soc_dapm_ignore_suspend(dapm, buff);

			aw882xx_register_i2c_error_callback(drvdata->aw_pa->channel,
				aw882xx_i2c_fail_callback);
			aw882xx_register_short_circuit_callback(drvdata->aw_pa->channel,
				aw882xx_short_circuit_callback);

			snd_soc_dapm_sync(dapm);
		}
#endif
	}

	return 0;
}

static int uaif_ops_hw_params(struct snd_pcm_substream *substream,
					struct snd_pcm_hw_params *params)
{
	return 0;
}

static const struct snd_soc_ops uaif_ops = {
	.hw_params = uaif_ops_hw_params,
};

static int dsif_hw_params(struct snd_pcm_substream *substream,
		struct snd_pcm_hw_params *hw_params)
{
	struct snd_soc_pcm_runtime *rtd = asoc_substream_to_rtd(substream);
	struct snd_soc_dai *cpu_dai = asoc_rtd_to_cpu(rtd, 0);
	int tx_slot[] = {0, 1};

	/* bclk ratio 64 for DSD64, 128 for DSD128 */
	snd_soc_dai_set_bclk_ratio(cpu_dai, 64);

	/* channel map 0 1 if left is first, 1 0 if right is first */
	snd_soc_dai_set_channel_map(cpu_dai, 2, tx_slot, 0, NULL);
	return 0;
}

static const struct snd_soc_ops dsif_ops = {
	.hw_params = dsif_hw_params,
};

static const struct snd_soc_ops udma_ops = {
};

static int exynos_late_probe(struct snd_soc_card *card)
{
	struct snd_soc_pcm_runtime *rtd;
	struct snd_soc_dai *dai;
	const char *name;
	struct snd_soc_dapm_context *dapm = &card->dapm;
	struct device_node *np = card->dev->of_node;
	int ret, i;

	if (of_property_read_bool(np, "samsung,routing")) {
		ret = snd_soc_of_parse_audio_routing(card, "samsung,routing");
		if (ret) {
			dev_err(card->dev, "snd_soc_of_parse_audio_routing failed: %d", ret);
			return ret;
		}
		ret = snd_soc_dapm_add_routes(dapm, card->of_dapm_routes,
						card->num_of_dapm_routes);
		if (ret < 0)
			dev_err(card->dev, "some routes failed to register: %d", ret);
	}

	snd_soc_dapm_ignore_suspend(dapm, "DMIC1");
	snd_soc_dapm_ignore_suspend(dapm, "DMIC2");
	snd_soc_dapm_ignore_suspend(dapm, "BLUETOOTH MIC");
	snd_soc_dapm_ignore_suspend(dapm, "BLUETOOTH SPK");
	snd_soc_dapm_ignore_suspend(dapm, "USB MIC");
	snd_soc_dapm_ignore_suspend(dapm, "USB SPK");
	snd_soc_dapm_ignore_suspend(dapm, "FWD MIC");
	snd_soc_dapm_ignore_suspend(dapm, "FWD SPK");
	snd_soc_dapm_ignore_suspend(dapm, "VTS Virtual Output");
	snd_soc_dapm_ignore_suspend(dapm, "VINPUT_FM");
	snd_soc_dapm_sync(dapm);

	for_each_card_rtds(card, rtd) {
		for_each_rtd_cpu_dais(rtd, i, dai) {
			dapm = snd_soc_component_get_dapm(dai->component);
			if (dai->playback_widget) {
				name = dai->driver->playback.stream_name;
				dev_dbg(card->dev, "ignore suspend: %s\n",
						name);
				snd_soc_dapm_ignore_suspend(dapm, name);
				snd_soc_dapm_sync(dapm);
			}
			if (dai->capture_widget) {
				name = dai->driver->capture.stream_name;
				dev_dbg(card->dev, "ignore suspend: %s\n",
						name);
				snd_soc_dapm_ignore_suspend(dapm, name);
				snd_soc_dapm_sync(dapm);
			}
		}


		for_each_rtd_codec_dais(rtd, i, dai) {
			dapm = snd_soc_component_get_dapm(dai->component);
			if (dai->playback_widget) {
				name = dai->driver->playback.stream_name;
				dev_dbg(card->dev, "ignore suspend: %s\n",
						name);
				snd_soc_dapm_ignore_suspend(dapm, name);
				snd_soc_dapm_sync(dapm);
			}
			if (dai->capture_widget) {
				name = dai->driver->capture.stream_name;
				dev_dbg(card->dev, "ignore suspend: %s\n",
						name);
				snd_soc_dapm_ignore_suspend(dapm, name);
				snd_soc_dapm_sync(dapm);
			}
		}
	}

	return 0;
}

static struct snd_soc_dai_link exynos_dai[100] = {
	{
		.name = "RDMA0",
		.stream_name = "RDMA0",
		.dynamic = 1,
		.ignore_suspend = 1,
		.trigger = {
			SND_SOC_DPCM_TRIGGER_POST,
			SND_SOC_DPCM_TRIGGER_PRE
		},
		.ops = &rdma_ops,
		.dpcm_playback = 1,
	},
	{
		.name = "RDMA1",
		.stream_name = "RDMA1",
		.dynamic = 1,
		.ignore_suspend = 1,
		.trigger = {
			SND_SOC_DPCM_TRIGGER_POST,
			SND_SOC_DPCM_TRIGGER_PRE
		},
		.ops = &rdma_ops,
		.dpcm_playback = 1,
	},
	{
		.name = "RDMA2",
		.stream_name = "RDMA2",
		.dynamic = 1,
		.ignore_suspend = 1,
		.trigger = {
			SND_SOC_DPCM_TRIGGER_POST,
			SND_SOC_DPCM_TRIGGER_PRE
		},
		.ops = &rdma_ops,
		.dpcm_playback = 1,
	},
	{
		.name = "RDMA3",
		.stream_name = "RDMA3",
		.dynamic = 1,
		.ignore_suspend = 1,
		.trigger = {
			SND_SOC_DPCM_TRIGGER_POST,
			SND_SOC_DPCM_TRIGGER_PRE
		},
		.ops = &rdma_ops,
		.dpcm_playback = 1,
	},
	{
		.name = "RDMA4",
		.stream_name = "RDMA4",
		.dynamic = 1,
		.ignore_suspend = 1,
		.trigger = {
			SND_SOC_DPCM_TRIGGER_POST,
			SND_SOC_DPCM_TRIGGER_PRE
		},
		.ops = &rdma_ops,
		.dpcm_playback = 1,
	},
	{
		.name = "RDMA5",
		.stream_name = "RDMA5",
		.dynamic = 1,
		.ignore_suspend = 1,
		.trigger = {
			SND_SOC_DPCM_TRIGGER_POST,
			SND_SOC_DPCM_TRIGGER_PRE
		},
		.ops = &rdma_ops,
		.dpcm_playback = 1,
	},
	{
		.name = "RDMA6",
		.stream_name = "RDMA6",
		.dynamic = 1,
		.ignore_suspend = 1,
		.trigger = {
			SND_SOC_DPCM_TRIGGER_POST,
			SND_SOC_DPCM_TRIGGER_PRE
		},
		.ops = &rdma_ops,
		.dpcm_playback = 1,
	},
	{
		.name = "RDMA7",
		.stream_name = "RDMA7",
		.dynamic = 1,
		.ignore_suspend = 1,
		.trigger = {
			SND_SOC_DPCM_TRIGGER_POST,
			SND_SOC_DPCM_TRIGGER_PRE
		},
		.ops = &rdma_ops,
		.dpcm_playback = 1,
	},
	{
		.name = "RDMA8",
		.stream_name = "RDMA8",
		.dynamic = 1,
		.ignore_suspend = 1,
		.trigger = {
			SND_SOC_DPCM_TRIGGER_POST,
			SND_SOC_DPCM_TRIGGER_PRE
		},
		.ops = &rdma_ops,
		.dpcm_playback = 1,
	},
	{
		.name = "RDMA9",
		.stream_name = "RDMA9",
		.dynamic = 1,
		.ignore_suspend = 1,
		.trigger = {
			SND_SOC_DPCM_TRIGGER_POST,
			SND_SOC_DPCM_TRIGGER_PRE
		},
		.ops = &rdma_ops,
		.dpcm_playback = 1,
	},
	{
		.name = "RDMA10",
		.stream_name = "RDMA10",
		.dynamic = 1,
		.ignore_suspend = 1,
		.trigger = {
			SND_SOC_DPCM_TRIGGER_POST,
			SND_SOC_DPCM_TRIGGER_PRE
		},
		.ops = &rdma_ops,
		.dpcm_playback = 1,
	},
	{
		.name = "RDMA11",
		.stream_name = "RDMA11",
		.dynamic = 1,
		.ignore_suspend = 1,
		.trigger = {
			SND_SOC_DPCM_TRIGGER_POST,
			SND_SOC_DPCM_TRIGGER_PRE
		},
		.ops = &rdma_ops,
		.dpcm_playback = 1,
	},
	{
		.name = "WDMA0",
		.stream_name = "WDMA0",
		.dynamic = 1,
		.ignore_suspend = 1,
		.trigger = {
			SND_SOC_DPCM_TRIGGER_POST,
			SND_SOC_DPCM_TRIGGER_PRE
		},
		.ops = &wdma_ops,
		.dpcm_capture = 1,
	},
	{
		.name = "WDMA1",
		.stream_name = "WDMA1",
		.dynamic = 1,
		.ignore_suspend = 1,
		.trigger = {
			SND_SOC_DPCM_TRIGGER_POST,
			SND_SOC_DPCM_TRIGGER_PRE
		},
		.ops = &wdma_ops,
		.dpcm_capture = 1,
	},
	{
		.name = "WDMA2",
		.stream_name = "WDMA2",
		.dynamic = 1,
		.ignore_suspend = 1,
		.trigger = {
			SND_SOC_DPCM_TRIGGER_POST,
			SND_SOC_DPCM_TRIGGER_PRE
		},
		.ops = &wdma_ops,
		.dpcm_capture = 1,
	},
	{
		.name = "WDMA3",
		.stream_name = "WDMA3",
		.dynamic = 1,
		.ignore_suspend = 1,
		.trigger = {
			SND_SOC_DPCM_TRIGGER_POST,
			SND_SOC_DPCM_TRIGGER_PRE
		},
		.ops = &wdma_ops,
		.dpcm_capture = 1,
	},
	{
		.name = "WDMA4",
		.stream_name = "WDMA4",
		.dynamic = 1,
		.ignore_suspend = 1,
		.trigger = {
			SND_SOC_DPCM_TRIGGER_POST,
			SND_SOC_DPCM_TRIGGER_PRE
		},
		.ops = &wdma_ops,
		.dpcm_capture = 1,
	},
	{
		.name = "WDMA5",
		.stream_name = "WDMA5",
		.dynamic = 1,
		.ignore_suspend = 1,
		.trigger = {
			SND_SOC_DPCM_TRIGGER_POST,
			SND_SOC_DPCM_TRIGGER_PRE
		},
		.ops = &wdma_ops,
		.dpcm_capture = 1,
	},
	{
		.name = "WDMA6",
		.stream_name = "WDMA6",
		.dynamic = 1,
		.ignore_suspend = 1,
		.trigger = {
			SND_SOC_DPCM_TRIGGER_POST,
			SND_SOC_DPCM_TRIGGER_PRE
		},
		.ops = &wdma_ops,
		.dpcm_capture = 1,
	},
	{
		.name = "WDMA7",
		.stream_name = "WDMA7",
		.dynamic = 1,
		.ignore_suspend = 1,
		.trigger = {
			SND_SOC_DPCM_TRIGGER_POST,
			SND_SOC_DPCM_TRIGGER_PRE
		},
		.ops = &wdma_ops,
		.dpcm_capture = 1,
	},
	{
		.name = "WDMA8",
		.stream_name = "WDMA8",
		.dynamic = 1,
		.ignore_suspend = 1,
		.trigger = {
			SND_SOC_DPCM_TRIGGER_POST,
			SND_SOC_DPCM_TRIGGER_PRE
		},
		.ops = &wdma_ops,
		.dpcm_capture = 1,
	},
	{
		.name = "WDMA9",
		.stream_name = "WDMA9",
		.dynamic = 1,
		.ignore_suspend = 1,
		.trigger = {
			SND_SOC_DPCM_TRIGGER_POST,
			SND_SOC_DPCM_TRIGGER_PRE
		},
		.ops = &wdma_ops,
		.dpcm_capture = 1,
	},
	{
		.name = "WDMA10",
		.stream_name = "WDMA10",
		.dynamic = 1,
		.ignore_suspend = 1,
		.trigger = {
			SND_SOC_DPCM_TRIGGER_POST,
			SND_SOC_DPCM_TRIGGER_PRE
		},
		.ops = &wdma_ops,
		.dpcm_capture = 1,
	},
	{
		.name = "WDMA11",
		.stream_name = "WDMA11",
		.dynamic = 1,
		.ignore_suspend = 1,
		.trigger = {
			SND_SOC_DPCM_TRIGGER_POST,
			SND_SOC_DPCM_TRIGGER_PRE
		},
		.ops = &wdma_ops,
		.dpcm_capture = 1,
	},
#if IS_ENABLED(CONFIG_SND_SOC_SAMSUNG_VTS)
	{
		.name = "VTS-Trigger",
		.stream_name = "VTS-Trigger",
		.ignore_suspend = 1,
		.ignore_pmdown_time = 1,
		.capture_only = true,
	},
	{
		.name = "VTS-Record",
		.stream_name = "VTS-Record",
		.ignore_suspend = 1,
		.ignore_pmdown_time = 1,
		.capture_only = true,
	},
#endif
#if IS_ENABLED(CONFIG_SND_SOC_SAMSUNG_DISPLAYPORT)
	{
		.name = "DP0 Audio",
		.stream_name = "DP0 Audio",
		.ignore_suspend = 1,
	},
	{
		.name = "DP1 Audio",
		.stream_name = "DP1 Audio",
		.ignore_suspend = 1,
	},
#endif
	{
		.name = "WDMA0 DUAL",
		.stream_name = "WDMA0 DUAL",
		.capture_only = 1,
		.ignore_suspend = 1,
	},
	{
		.name = "WDMA1 DUAL",
		.stream_name = "WDMA1 DUAL",
		.capture_only = 1,
		.ignore_suspend = 1,
	},
	{
		.name = "WDMA2 DUAL",
		.stream_name = "WDMA2 DUAL",
		.capture_only = 1,
		.ignore_suspend = 1,
	},
	{
		.name = "WDMA3 DUAL",
		.stream_name = "WDMA3 DUAL",
		.capture_only = 1,
		.ignore_suspend = 1,
	},
	{
		.name = "WDMA4 DUAL",
		.stream_name = "WDMA4 DUAL",
		.capture_only = 1,
		.ignore_suspend = 1,
	},
	{
		.name = "WDMA5 DUAL",
		.stream_name = "WDMA5 DUAL",
		.capture_only = 1,
		.ignore_suspend = 1,
	},
	{
		.name = "WDMA6 DUAL",
		.stream_name = "WDMA6 DUAL",
		.capture_only = 1,
		.ignore_suspend = 1,
	},
	{
		.name = "WDMA7 DUAL",
		.stream_name = "WDMA7 DUAL",
		.capture_only = 1,
		.ignore_suspend = 1,
	},
	{
		.name = "WDMA8 DUAL",
		.stream_name = "WDMA8 DUAL",
		.capture_only = 1,
		.ignore_suspend = 1,
	},
	{
		.name = "WDMA9 DUAL",
		.stream_name = "WDMA9 DUAL",
		.capture_only = 1,
		.ignore_suspend = 1,
	},
	{
		.name = "WDMA10 DUAL",
		.stream_name = "WDMA10 DUAL",
		.capture_only = 1,
		.ignore_suspend = 1,
	},
	{
		.name = "WDMA11 DUAL",
		.stream_name = "WDMA11 DUAL",
		.capture_only = 1,
		.ignore_suspend = 1,
	},
	{
		.name = "DEBUG0",
		.stream_name = "DEBUG0",
		.capture_only = 1,
		.ignore_suspend = 1,
	},
	{
		.name = "DEBUG1",
		.stream_name = "DEBUG1",
		.capture_only = 1,
		.ignore_suspend = 1,
	},
	{
		.name = "DEBUG2",
		.stream_name = "DEBUG2",
		.capture_only = 1,
		.ignore_suspend = 1,
	},
	{
		.name = "DEBUG3",
		.stream_name = "DEBUG3",
		.capture_only = 1,
		.ignore_suspend = 1,
	},
	{
		.name = "DEBUG4",
		.stream_name = "DEBUG4",
		.capture_only = 1,
		.ignore_suspend = 1,
	},
	{
		.name = "DEBUG5",
		.stream_name = "DEBUG5",
		.capture_only = 1,
		.ignore_suspend = 1,
	},
	{
		.name = "UAIF0",
		.stream_name = "UAIF0",
		.id = ABOX_UAIF_DAI_ID(0, 0),
		.no_pcm = 1,
		.ignore_suspend = 1,
		.ignore_pmdown_time = 1,
		.be_hw_params_fixup = abox_hw_params_fixup_helper,
		.init = uaif0_init,
		.ops = &uaif_ops,
		.dpcm_playback = 1,
		.dpcm_capture = 1,
	},
	{
		.name = "UAIF1",
		.stream_name = "UAIF1",
		.id = ABOX_UAIF_DAI_ID(0, 1),
		.no_pcm = 1,
		.ignore_suspend = 1,
		.ignore_pmdown_time = 1,
		.be_hw_params_fixup = abox_hw_params_fixup_helper,
		.init = uaif1_init,
		.ops = &uaif_ops,
		.dpcm_playback = 1,
		.dpcm_capture = 1,
	},
	{
		.name = "UAIF2",
		.stream_name = "UAIF2",
		.id = ABOX_UAIF_DAI_ID(0, 2),
		.no_pcm = 1,
		.ignore_suspend = 1,
		.ignore_pmdown_time = 1,
		.be_hw_params_fixup = abox_hw_params_fixup_helper,
		.ops = &uaif_ops,
		.dpcm_playback = 1,
		.dpcm_capture = 1,
	},
	{
		.name = "UAIF3",
		.stream_name = "UAIF3",
		.id = ABOX_UAIF_DAI_ID(0, 3),
		.no_pcm = 1,
		.ignore_suspend = 1,
		.ignore_pmdown_time = 1,
		.be_hw_params_fixup = abox_hw_params_fixup_helper,
		.ops = &uaif_ops,
		.dpcm_playback = 1,
		.dpcm_capture = 1,
	},
	{
		.name = "UAIF4",
		.stream_name = "UAIF4",
		.id = ABOX_UAIF_DAI_ID(0, 4),
		.no_pcm = 1,
		.ignore_suspend = 1,
		.ignore_pmdown_time = 1,
		.be_hw_params_fixup = abox_hw_params_fixup_helper,
		.ops = &uaif_ops,
		.dpcm_playback = 1,
		.dpcm_capture = 1,
	},
	{
		.name = "UAIF5",
		.stream_name = "UAIF5",
		.id = ABOX_UAIF_DAI_ID(0, 5),
		.no_pcm = 1,
		.ignore_suspend = 1,
		.ignore_pmdown_time = 1,
		.be_hw_params_fixup = abox_hw_params_fixup_helper,
		.ops = &uaif_ops,
		.dpcm_playback = 1,
		.dpcm_capture = 1,
	},
	{
		.name = "UAIF6",
		.stream_name = "UAIF6",
		.id = ABOX_UAIF_DAI_ID(0, 6),
		.no_pcm = 1,
		.ignore_suspend = 1,
		.ignore_pmdown_time = 1,
		.be_hw_params_fixup = abox_hw_params_fixup_helper,
		.ops = &uaif_ops,
		.dpcm_playback = 1,
		.dpcm_capture = 1,
	},
	{
		.name = "DSIF",
		.stream_name = "DSIF",
		.no_pcm = 1,
		.ignore_suspend = 1,
		.ignore_pmdown_time = 1,
		.be_hw_params_fixup = abox_hw_params_fixup_helper,
		.ops = &dsif_ops,
		.dpcm_playback = 1,
	},
	{
		.name = "SPDY",
		.stream_name = "SPDY",
		.no_pcm = 1,
		.ignore_suspend = 1,
		.ignore_pmdown_time = 1,
		.be_hw_params_fixup = abox_hw_params_fixup_helper,
		.dpcm_capture = 1,
	},
	{
		.name = "UDMA RD0",
		.stream_name = "UDMA RD0",
		.no_pcm = 1,
		.ignore_suspend = 1,
		.ignore_pmdown_time = 1,
		.be_hw_params_fixup = abox_hw_params_fixup_helper,
		.ops = &udma_ops,
		.dpcm_capture = 1,
	},
	{
		.name = "UDMA RD1",
		.stream_name = "UDMA RD1",
		.no_pcm = 1,
		.ignore_suspend = 1,
		.ignore_pmdown_time = 1,
		.be_hw_params_fixup = abox_hw_params_fixup_helper,
		.ops = &udma_ops,
		.dpcm_capture = 1,
	},
	{
		.name = "UDMA WR0",
		.stream_name = "UDMA WR0",
		.no_pcm = 1,
		.ignore_suspend = 1,
		.ignore_pmdown_time = 1,
		.be_hw_params_fixup = abox_hw_params_fixup_helper,
		.ops = &udma_ops,
		.dpcm_playback = 1,
	},
	{
		.name = "UDMA WR1",
		.stream_name = "UDMA WR1",
		.no_pcm = 1,
		.ignore_suspend = 1,
		.ignore_pmdown_time = 1,
		.be_hw_params_fixup = abox_hw_params_fixup_helper,
		.ops = &udma_ops,
		.dpcm_playback = 1,
	},
	{
		.name = "UDMA WR0 DUAL",
		.stream_name = "UDMA WR0 DUAL",
		.capture_only = 1,
		.ignore_suspend = 1,
	},
	{
		.name = "UDMA WR1 DUAL",
		.stream_name = "UDMA WR1 DUAL",
		.capture_only = 1,
		.ignore_suspend = 1,
	},
	{
		.name = "UDMA DEBUG0",
		.stream_name = "UDMA DEBUG0",
		.capture_only = 1,
		.ignore_suspend = 1,
	},
	{
		.name = "RDMA0 BE",
		.stream_name = "RDMA0 BE",
		.id = ABOX_BE_DAI_ID(0, 0),
		.no_pcm = 1,
		.ignore_suspend = 1,
		.ignore_pmdown_time = 1,
		.be_hw_params_fixup = abox_hw_params_fixup_helper,
		.dpcm_playback = 1,
		.dpcm_capture = 1,
	},
	{
		.name = "RDMA1 BE",
		.stream_name = "RDMA1 BE",
		.id = ABOX_BE_DAI_ID(0, 1),
		.no_pcm = 1,
		.ignore_suspend = 1,
		.ignore_pmdown_time = 1,
		.be_hw_params_fixup = abox_hw_params_fixup_helper,
		.dpcm_playback = 1,
		.dpcm_capture = 1,
	},
	{
		.name = "RDMA2 BE",
		.stream_name = "RDMA2 BE",
		.id = ABOX_BE_DAI_ID(0, 2),
		.no_pcm = 1,
		.ignore_suspend = 1,
		.ignore_pmdown_time = 1,
		.be_hw_params_fixup = abox_hw_params_fixup_helper,
		.dpcm_playback = 1,
		.dpcm_capture = 1,
	},
	{
		.name = "RDMA3 BE",
		.stream_name = "RDMA3 BE",
		.id = ABOX_BE_DAI_ID(0, 3),
		.no_pcm = 1,
		.ignore_suspend = 1,
		.ignore_pmdown_time = 1,
		.be_hw_params_fixup = abox_hw_params_fixup_helper,
		.dpcm_playback = 1,
		.dpcm_capture = 1,
	},
	{
		.name = "RDMA4 BE",
		.stream_name = "RDMA4 BE",
		.id = ABOX_BE_DAI_ID(0, 4),
		.no_pcm = 1,
		.ignore_suspend = 1,
		.ignore_pmdown_time = 1,
		.be_hw_params_fixup = abox_hw_params_fixup_helper,
		.dpcm_playback = 1,
		.dpcm_capture = 1,
	},
	{
		.name = "RDMA5 BE",
		.stream_name = "RDMA5 BE",
		.id = ABOX_BE_DAI_ID(0, 5),
		.no_pcm = 1,
		.ignore_suspend = 1,
		.ignore_pmdown_time = 1,
		.be_hw_params_fixup = abox_hw_params_fixup_helper,
		.dpcm_playback = 1,
		.dpcm_capture = 1,
	},
	{
		.name = "RDMA6 BE",
		.stream_name = "RDMA6 BE",
		.id = ABOX_BE_DAI_ID(0, 6),
		.no_pcm = 1,
		.ignore_suspend = 1,
		.ignore_pmdown_time = 1,
		.be_hw_params_fixup = abox_hw_params_fixup_helper,
		.dpcm_playback = 1,
		.dpcm_capture = 1,
	},
	{
		.name = "RDMA7 BE",
		.stream_name = "RDMA7 BE",
		.id = ABOX_BE_DAI_ID(0, 7),
		.no_pcm = 1,
		.ignore_suspend = 1,
		.ignore_pmdown_time = 1,
		.be_hw_params_fixup = abox_hw_params_fixup_helper,
		.dpcm_playback = 1,
		.dpcm_capture = 1,
	},
	{
		.name = "RDMA8 BE",
		.stream_name = "RDMA8 BE",
		.id = ABOX_BE_DAI_ID(0, 8),
		.no_pcm = 1,
		.ignore_suspend = 1,
		.ignore_pmdown_time = 1,
		.be_hw_params_fixup = abox_hw_params_fixup_helper,
		.dpcm_playback = 1,
		.dpcm_capture = 1,
	},
	{
		.name = "RDMA9 BE",
		.stream_name = "RDMA9 BE",
		.id = ABOX_BE_DAI_ID(0, 9),
		.no_pcm = 1,
		.ignore_suspend = 1,
		.ignore_pmdown_time = 1,
		.be_hw_params_fixup = abox_hw_params_fixup_helper,
		.dpcm_playback = 1,
		.dpcm_capture = 1,
	},
	{
		.name = "RDMA10 BE",
		.stream_name = "RDMA10 BE",
		.id = ABOX_BE_DAI_ID(0, 10),
		.no_pcm = 1,
		.ignore_suspend = 1,
		.ignore_pmdown_time = 1,
		.be_hw_params_fixup = abox_hw_params_fixup_helper,
		.dpcm_playback = 1,
		.dpcm_capture = 1,
	},
	{
		.name = "RDMA11 BE",
		.stream_name = "RDMA11 BE",
		.id = ABOX_BE_DAI_ID(0, 11),
		.no_pcm = 1,
		.ignore_suspend = 1,
		.ignore_pmdown_time = 1,
		.be_hw_params_fixup = abox_hw_params_fixup_helper,
		.dpcm_playback = 1,
		.dpcm_capture = 1,
	},
	{
		.name = "WDMA0 BE",
		.stream_name = "WDMA0 BE",
		.id = ABOX_BE_DAI_ID(1, 0),
		.no_pcm = 1,
		.ignore_suspend = 1,
		.ignore_pmdown_time = 1,
		.be_hw_params_fixup = abox_hw_params_fixup_helper,
		.dpcm_playback = 1,
		.dpcm_capture = 1,
	},
	{
		.name = "WDMA1 BE",
		.stream_name = "WDMA1 BE",
		.id = ABOX_BE_DAI_ID(1, 1),
		.no_pcm = 1,
		.ignore_suspend = 1,
		.ignore_pmdown_time = 1,
		.be_hw_params_fixup = abox_hw_params_fixup_helper,
		.dpcm_playback = 1,
		.dpcm_capture = 1,
	},
	{
		.name = "WDMA2 BE",
		.stream_name = "WDMA2 BE",
		.id = ABOX_BE_DAI_ID(1, 2),
		.no_pcm = 1,
		.ignore_suspend = 1,
		.ignore_pmdown_time = 1,
		.be_hw_params_fixup = abox_hw_params_fixup_helper,
		.dpcm_playback = 1,
		.dpcm_capture = 1,
	},
	{
		.name = "WDMA3 BE",
		.stream_name = "WDMA3 BE",
		.id = ABOX_BE_DAI_ID(1, 3),
		.no_pcm = 1,
		.ignore_suspend = 1,
		.ignore_pmdown_time = 1,
		.be_hw_params_fixup = abox_hw_params_fixup_helper,
		.dpcm_playback = 1,
		.dpcm_capture = 1,
	},
	{
		.name = "WDMA4 BE",
		.stream_name = "WDMA4 BE",
		.id = ABOX_BE_DAI_ID(1, 4),
		.no_pcm = 1,
		.ignore_suspend = 1,
		.ignore_pmdown_time = 1,
		.be_hw_params_fixup = abox_hw_params_fixup_helper,
		.dpcm_playback = 1,
		.dpcm_capture = 1,
	},
	{
		.name = "WDMA5 BE",
		.stream_name = "WDMA5 BE",
		.id = ABOX_BE_DAI_ID(1, 5),
		.no_pcm = 1,
		.ignore_suspend = 1,
		.ignore_pmdown_time = 1,
		.be_hw_params_fixup = abox_hw_params_fixup_helper,
		.dpcm_playback = 1,
		.dpcm_capture = 1,
	},
	{
		.name = "WDMA6 BE",
		.stream_name = "WDMA6 BE",
		.id = ABOX_BE_DAI_ID(1, 6),
		.no_pcm = 1,
		.ignore_suspend = 1,
		.ignore_pmdown_time = 1,
		.be_hw_params_fixup = abox_hw_params_fixup_helper,
		.dpcm_playback = 1,
		.dpcm_capture = 1,
	},
	{
		.name = "WDMA7 BE",
		.stream_name = "WDMA7_BE",
		.id = ABOX_BE_DAI_ID(1, 7),
		.no_pcm = 1,
		.ignore_suspend = 1,
		.ignore_pmdown_time = 1,
		.be_hw_params_fixup = abox_hw_params_fixup_helper,
		.dpcm_playback = 1,
		.dpcm_capture = 1,
	},
	{
		.name = "WDMA8 BE",
		.stream_name = "WDMA8_BE",
		.id = ABOX_BE_DAI_ID(1, 8),
		.no_pcm = 1,
		.ignore_suspend = 1,
		.ignore_pmdown_time = 1,
		.be_hw_params_fixup = abox_hw_params_fixup_helper,
		.dpcm_playback = 1,
		.dpcm_capture = 1,
	},
	{
		.name = "WDMA9 BE",
		.stream_name = "WDMA9_BE",
		.id = ABOX_BE_DAI_ID(1, 9),
		.no_pcm = 1,
		.ignore_suspend = 1,
		.ignore_pmdown_time = 1,
		.be_hw_params_fixup = abox_hw_params_fixup_helper,
		.dpcm_playback = 1,
		.dpcm_capture = 1,
	},
	{
		.name = "WDMA10 BE",
		.stream_name = "WDMA10_BE",
		.id = ABOX_BE_DAI_ID(1, 10),
		.no_pcm = 1,
		.ignore_suspend = 1,
		.ignore_pmdown_time = 1,
		.be_hw_params_fixup = abox_hw_params_fixup_helper,
		.dpcm_playback = 1,
		.dpcm_capture = 1,
	},
	{
		.name = "WDMA11 BE",
		.stream_name = "WDMA11_BE",
		.id = ABOX_BE_DAI_ID(1, 11),
		.no_pcm = 1,
		.ignore_suspend = 1,
		.ignore_pmdown_time = 1,
		.be_hw_params_fixup = abox_hw_params_fixup_helper,
		.dpcm_playback = 1,
		.dpcm_capture = 1,
	},
	{
		.name = "USB",
		.stream_name = "USB",
		.no_pcm = 1,
		.ignore_suspend = 1,
		.ignore_pmdown_time = 1,
		.dpcm_playback = 1,
		.dpcm_capture = 1,
	},
	{
		.name = "FWD",
		.stream_name = "FWD",
		.no_pcm = 1,
		.ignore_suspend = 1,
		.ignore_pmdown_time = 1,
		.dpcm_playback = 1,
		.dpcm_capture = 1,
	},
	{
	},
};

static int get_sound_wakelock(struct snd_kcontrol *kcontrol,
			struct snd_ctl_elem_value *ucontrol)
{
	struct _drvdata *drvdata = &exynos_drvdata;
	unsigned int val = drvdata->ws->active;

	dev_dbg(drvdata->dev, "%s: %d\n", __func__, val);

	ucontrol->value.integer.value[0] = val;

	return 0;
}

static int set_sound_wakelock(struct snd_kcontrol *kcontrol,
			struct snd_ctl_elem_value *ucontrol)
{
	struct _drvdata *drvdata = &exynos_drvdata;
	unsigned int val = (unsigned int)ucontrol->value.integer.value[0];

	dev_info(drvdata->dev, "%s: %d\n", __func__, val);

	if (val)
		__pm_stay_awake(drvdata->ws);
	else
		__pm_relax(drvdata->ws);

	return 0;
}

static const char * const vts_output_texts[] = {
	"None",
	"DMIC1",
};

static const struct soc_enum vts_output_enum =
	SOC_ENUM_SINGLE(SND_SOC_NOPM, 0, ARRAY_SIZE(vts_output_texts),
			vts_output_texts);

static const struct snd_kcontrol_new vts_output_mux[] = {
	SOC_DAPM_ENUM("VTS Virtual Output Mux", vts_output_enum),
};

static const struct snd_kcontrol_new exynos_controls[] = {
	SOC_DAPM_PIN_SWITCH("DMIC1"),
	SOC_DAPM_PIN_SWITCH("DMIC2"),
	SOC_SINGLE_BOOL_EXT("Sound Wakelock",
			0, get_sound_wakelock, set_sound_wakelock),
};

static const struct snd_soc_dapm_widget exynos_widgets[] = {
	SND_SOC_DAPM_MIC("DMIC1", NULL),
	SND_SOC_DAPM_MIC("DMIC2", NULL),
	SND_SOC_DAPM_MIC("BLUETOOTH MIC", NULL),
	SND_SOC_DAPM_SPK("BLUETOOTH SPK", NULL),
	SND_SOC_DAPM_MIC("USB MIC", NULL),
	SND_SOC_DAPM_SPK("USB SPK", NULL),
	SND_SOC_DAPM_MIC("FWD MIC", NULL),
	SND_SOC_DAPM_SPK("FWD SPK", NULL),
	SND_SOC_DAPM_OUTPUT("VTS Virtual Output"),
	SND_SOC_DAPM_MUX("VTS Virtual Output Mux", SND_SOC_NOPM, 0, 0,
			&vts_output_mux[0]),
	SND_SOC_DAPM_INPUT("VINPUT_FM"),
};

static const struct snd_soc_dapm_route exynos_routes[] = {
	{"VTS Virtual Output Mux", "DMIC1", "DMIC1"},
};

static struct snd_soc_codec_conf codec_conf[CODEC_CONF_MAX];

static struct snd_soc_aux_dev aux_dev[AUX_DEV_MAX];

static struct snd_soc_card exynos_card = {
	.name = "exynos-card",
	.owner = THIS_MODULE,
	.dai_link = exynos_dai,
	.num_links = ARRAY_SIZE(exynos_dai),

	.late_probe = exynos_late_probe,

	.controls = exynos_controls,
	.num_controls = ARRAY_SIZE(exynos_controls),
	.dapm_widgets = exynos_widgets,
	.num_dapm_widgets = ARRAY_SIZE(exynos_widgets),
	.dapm_routes = exynos_routes,
	.num_dapm_routes = ARRAY_SIZE(exynos_routes),

	.drvdata = (void *)&exynos_drvdata,

	.codec_conf = codec_conf,
	.num_configs = ARRAY_SIZE(codec_conf),

	.aux_dev = aux_dev,
	.num_aux_devs = ARRAY_SIZE(aux_dev),
};

#if IS_ENABLED(CONFIG_SND_SOC_SAMSUNG_AUDIO)
static int sec_dai_link_codecs_component(struct device *dev, struct device_node *np,
		struct snd_soc_dai_link *dai_link)
{
	struct snd_soc_dai_link_component *component;
	struct of_phandle_args args;
	int index, num_codecs;
	int ret;

	num_codecs = of_count_phandle_with_args(np, "sound-dai", "#sound-dai-cells");

	dev_info(dev, "%s : num_codecs : %d dai_link_id : %x\n", __func__, num_codecs, dai_link->id);

	if (num_codecs <= 0)
		return -EINVAL;

	component = devm_kcalloc(dev, num_codecs, sizeof(*component), GFP_KERNEL);
	if (!component) {
		dai_link->codecs = NULL;
		dai_link->num_codecs = 0;
		return -ENOMEM;
	}
	dai_link->codecs = component;
	dai_link->num_codecs = num_codecs;

	/* checks each codec component in a given DAI link to see if it has been initialized correctly. */
	for_each_link_codecs(dai_link, index, component) {
		dev_info(dev, "%s : dai_link %s[%d] Codec Check\n",
					__func__, dai_link->name, index);

		of_parse_phandle_with_args(np, "sound-dai",
						 "#sound-dai-cells", index, &args);
		component->of_node = args.np;

		ret = snd_soc_get_dai_name(&args, &component->dai_name);
		if (ret < 0) {
			dev_err(dev, "%s : dai_link %s[%d] component init fail %d\n",
					__func__, dai_link->name, index, ret);
			component->name = "snd-soc-dummy";
			component->dai_name = "snd-soc-dummy-dai";
			component->of_node = NULL;
			dev_err(dev, "%s : Dummy Component (%s : %s)\n",
					__func__, component->name, component->dai_name);
		} else {
			dev_info(dev, "%s : dai_link %s[%d] component init success %d\n",
					__func__, dai_link->name, index, ret);
			dev_info(dev, "%s : Normal Component (%s : %s)\n",
					__func__, component->name, component->dai_name);
		}

		sdp_boot_print("%s: %s init %s\n",
				dai_link->name, component->dai_name, ret ? "FAIL" : "SUCCESS");
		if(dai_link->id == ABOX_UAIF_DAI_ID(0, 1)) {
			send_amp_ready_ev(index, ret ? INIT_FAIL : INIT_SUCCESS);
		}

	}
	return 0;
}
#endif

static int read_platform(struct device_node *np, struct device *dev,
		struct snd_soc_dai_link *dai_link)
{
	int ret = 0;
	struct snd_soc_dai_link_component *platform;

	np = of_get_child_by_name(np, "platform");
	if (!np)
		return 0;

	platform = devm_kcalloc(dev, 1, sizeof(*platform), GFP_KERNEL);
	if (!platform) {
		ret = -ENOMEM;
		goto out;
	}

	platform->of_node = of_parse_phandle(np, "sound-dai", 0);
	if (!platform->of_node) {
		ret = -ENODEV;
		goto out;
	}
out:
	if (ret < 0) {
		if (platform)
			devm_kfree(dev, platform);
	} else {
		dai_link->platforms = platform;
		dai_link->num_platforms = 1;
	}
	of_node_put(np);

	return ret;
}

static int read_cpu(struct device_node *np, struct device *dev,
		struct snd_soc_dai_link *dai_link)
{
	int ret = 0;
	struct snd_soc_dai_link_component *cpu;

	np = of_get_child_by_name(np, "cpu");
	if (!np)
		return 0;

	cpu = devm_kcalloc(dev, 1, sizeof(*cpu), GFP_KERNEL);
	if (!cpu) {
		ret = -ENOMEM;
		goto out;
	}

	cpu->of_node = of_parse_phandle(np, "sound-dai", 0);
	if (!cpu->of_node) {
		ret = -ENODEV;
		goto out;
	}

	ret = snd_soc_of_get_dai_name(np, &cpu->dai_name);
out:
	if (ret < 0) {
		if (cpu)
			devm_kfree(dev, cpu);
	} else {
		dai_link->cpus = cpu;
		dai_link->num_cpus = 1;
	}
	of_node_put(np);

	return ret;
}

SND_SOC_DAILINK_DEF(dailink_comp_dummy, DAILINK_COMP_ARRAY(COMP_DUMMY()));

static int read_codec(struct device_node *np, struct device *dev,
		struct snd_soc_dai_link *dai_link)
{
	int ret;

	np = of_get_child_by_name(np, "codec");
	if (!np) {
		dai_link->codecs = dailink_comp_dummy;
		dai_link->num_codecs = ARRAY_SIZE(dailink_comp_dummy);
		return 0;
	}

#if IS_ENABLED(CONFIG_SND_SOC_SAMSUNG_AUDIO)
	ret = sec_dai_link_codecs_component(dev, np, dai_link);
#else
	ret = snd_soc_of_get_dai_link_codecs(dev, np, dai_link);
#endif
	of_node_put(np);

	return ret;
}

static void exynos_register_card_work_func(struct work_struct *work)
{
	struct snd_soc_card *card = &exynos_card;
	int ret;

	ret = devm_snd_soc_register_card(card->dev, card);
	if (ret)
		dev_err(card->dev, "sound card register failed: %d\n", ret);

#if IS_ENABLED(CONFIG_SND_SOC_SAMSUNG_AUDIO) && SUPPORT_AMP_READY_CALLBACK
	audio_register_ready_cb(get_audio_amp_ready);
#endif
}
DECLARE_WORK(exynos_register_card_work, exynos_register_card_work_func);

static int exynos_sound_probe(struct platform_device *pdev)
{
	struct snd_soc_card *card = &exynos_card;
	struct _drvdata *drvdata = snd_soc_card_get_drvdata(card);
	struct device_node *np = pdev->dev.of_node;
	struct device_node *dai;
	struct snd_soc_dai_link *link;
	int i, ret, nlink = 0;

	card->dev = &pdev->dev;
	drvdata->dev = card->dev;
	drvdata->ws = wakeup_source_register(NULL, "exynos-sound");

	snd_soc_card_set_drvdata(card, drvdata);

	for_each_child_of_node(np, dai) {
		link = &exynos_dai[nlink];

		if (!link->name)
			link->name = dai->name;
		if (!link->stream_name)
			link->stream_name = dai->name;

		if (!link->cpus) {
			ret = read_cpu(dai, card->dev, link);
			if (ret < 0) {
				dev_err(card->dev, "Failed to parse cpu DAI for %s: %d\n",
						dai->name, ret);
				return ret;
			}
		}

		if (!link->platforms) {
			ret = read_platform(dai, card->dev, link);
			if (ret < 0) {
				dev_warn(card->dev, "Failed to parse platform for %s: %d\n",
						dai->name, ret);
				ret = 0;
			}
		}

		if (!link->codecs) {
			ret = read_codec(dai, card->dev, link);
			if (ret < 0) {
				dev_warn(card->dev, "Failed to parse codec DAI for %s: %d\n",
						dai->name, ret);
				ret = 0;
			}
		}

		link->dai_fmt = snd_soc_of_parse_daifmt(dai, NULL, NULL, NULL);

		if (++nlink == card->num_links)
			break;
	}

	if (!nlink) {
		dev_err(card->dev, "No DAIs specified\n");
		return -EINVAL;
	}

	/* Dummy pcm to adjust ID of PCM added by topology */
	for (; nlink < card->num_links; nlink++) {
		link = &exynos_dai[nlink];

		if (!link->name)
			link->name = devm_kasprintf(card->dev, GFP_KERNEL,
					"dummy%d", nlink);
		if (!link->stream_name)
			link->stream_name = link->name;

		if (!link->cpus) {
			link->cpus = dailink_comp_dummy;
			link->num_cpus = ARRAY_SIZE(dailink_comp_dummy);
		}

		if (!link->codecs) {
			link->codecs = dailink_comp_dummy;
			link->num_codecs = ARRAY_SIZE(dailink_comp_dummy);
		}

		link->no_pcm = 1;
		link->ignore_suspend = 1;
	}

	for (i = 0; i < ARRAY_SIZE(codec_conf); i++) {
		codec_conf[i].dlc.of_node = of_parse_phandle(np, "samsung,codec",
				i);
		if (!codec_conf[i].dlc.of_node)
			break;

		ret = of_property_read_string_index(np, "samsung,prefix", i,
				&codec_conf[i].name_prefix);
		if (ret < 0)
			codec_conf[i].name_prefix = "";
	}
	card->num_configs = i;

	for (i = 0; i < ARRAY_SIZE(aux_dev); i++) {
		aux_dev[i].dlc.of_node = of_parse_phandle(np, "samsung,aux", i);
		if (!aux_dev[i].dlc.of_node)
			break;
	}
	card->num_aux_devs = i;

	schedule_work(&exynos_register_card_work);

	return ret;
}

static int exynos_sound_remove(struct platform_device *pdev)
{
	struct snd_soc_card *card;
	struct _drvdata *drvdata;
	struct snd_soc_dai_link *dai_link;
	struct snd_soc_dai_link_component *cpu, *platform;
	int i;

	card = platform_get_drvdata(pdev);
	if (!card)
		return 0;

	drvdata = snd_soc_card_get_drvdata(card);
	if (!drvdata)
		return 0;

	for (dai_link = exynos_dai; dai_link - exynos_dai <
			ARRAY_SIZE(exynos_dai); dai_link++) {
		for_each_link_cpus(dai_link, i, cpu) {
			if (cpu->of_node)
				of_node_put(cpu->of_node);
		}

		for_each_link_platforms(dai_link, i, platform) {
			if (platform->of_node)
				of_node_put(platform->of_node);
		}

		snd_soc_of_put_dai_link_codecs(dai_link);
	}

	wakeup_source_unregister(drvdata->ws);

	return 0;
}

#if IS_ENABLED(CONFIG_OF)
static const struct of_device_id exynos_sound_of_match[] = {
	{ .compatible = "samsung,exynos8825-audio", },
	{},
};
MODULE_DEVICE_TABLE(of, exynos_sound_of_match);
#endif /* CONFIG_OF */

static struct platform_driver exynos_sound_driver = {
	.driver		= {
		.name	= "exynos8825-audio",
		.owner	= THIS_MODULE,
		.pm = &snd_soc_pm_ops,
		.of_match_table = of_match_ptr(exynos_sound_of_match),
	},

	.probe		= exynos_sound_probe,
	.remove		= exynos_sound_remove,
};

module_platform_driver(exynos_sound_driver);

MODULE_DESCRIPTION("ALSA SoC Exynos Sound Driver");
MODULE_AUTHOR("Charles Keepax <ckeepax@opensource.wolfsonmicro.com>");
MODULE_AUTHOR("Shinhyung Kang <s47.kang@samsung.com>");
MODULE_LICENSE("GPL");
MODULE_ALIAS("platform:exynos8825-sound");
