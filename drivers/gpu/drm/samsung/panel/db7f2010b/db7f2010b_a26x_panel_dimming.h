/*
 * linux/drivers/video/fbdev/exynos/panel/db7f2010b/db7f2010b_a26x_panel_dimming.h
 *
 * Header file for DB7F2010B Dimming Driver
 *
 * Copyright (c) 2016 Samsung Electronics
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#ifndef __DB7F2010B_A26X_PANEL_DIMMING_H___
#define __DB7F2010B_A26X_PANEL_DIMMING_H___
#include "../dimming.h"
#include "../panel_dimming.h"

/*
 * PANEL INFORMATION
 * LDI : DB7F2010B
 * PANEL : PRE
 */
#define DB7F2010B_A26X_NR_STEP (DB7F2010B_A26X_NR_LUMINANCE)
#define DB7F2010B_A26X_HBM_STEP (DB7F2010B_A26X_NR_HBM_LUMINANCE)
#define DB7F2010B_A26X_TOTAL_STEP (DB7F2010B_A26X_NR_STEP + DB7F2010B_A26X_HBM_STEP)

static unsigned int a26x_brt_tbl[DB7F2010B_A26X_TOTAL_STEP] = {
	BRT(0), BRT(1), BRT(2), BRT(3), BRT(4), BRT(5), BRT(6), BRT(7), BRT(8), BRT(9), BRT(10),
	BRT(11), BRT(12), BRT(13), BRT(14), BRT(15), BRT(16), BRT(17), BRT(18), BRT(19), BRT(20),
	BRT(21), BRT(22), BRT(23), BRT(24), BRT(25), BRT(26), BRT(27), BRT(28), BRT(29), BRT(30),
	BRT(31), BRT(32), BRT(33), BRT(34), BRT(35), BRT(36), BRT(37), BRT(38), BRT(39), BRT(40),
	BRT(41), BRT(42), BRT(43), BRT(44), BRT(45), BRT(46), BRT(47), BRT(48), BRT(49), BRT(50),
	BRT(51), BRT(52), BRT(53), BRT(54), BRT(55), BRT(56), BRT(57), BRT(58), BRT(59), BRT(60),
	BRT(61), BRT(62), BRT(63), BRT(64), BRT(65), BRT(66), BRT(67), BRT(68), BRT(69), BRT(70),
	BRT(71), BRT(72), BRT(73), BRT(74), BRT(75), BRT(76), BRT(77), BRT(78), BRT(79), BRT(80),
	BRT(81), BRT(82), BRT(83), BRT(84), BRT(85), BRT(86), BRT(87), BRT(88), BRT(89), BRT(90),
	BRT(91), BRT(92), BRT(93), BRT(94), BRT(95), BRT(96), BRT(97), BRT(98), BRT(99), BRT(100),
	BRT(101), BRT(102), BRT(103), BRT(104), BRT(105), BRT(106), BRT(107), BRT(108), BRT(109), BRT(110),
	BRT(111), BRT(112), BRT(113), BRT(114), BRT(115), BRT(116), BRT(117), BRT(118), BRT(119), BRT(120),
	BRT(121), BRT(122), BRT(123), BRT(124), BRT(125), BRT(126), BRT(127), BRT(128), BRT(129), BRT(130),
	BRT(131), BRT(132), BRT(133), BRT(134), BRT(135), BRT(136), BRT(137), BRT(138), BRT(139), BRT(140),
	BRT(141), BRT(142), BRT(143), BRT(144), BRT(145), BRT(146), BRT(147), BRT(148), BRT(149), BRT(150),
	BRT(151), BRT(152), BRT(153), BRT(154), BRT(155), BRT(156), BRT(157), BRT(158), BRT(159), BRT(160),
	BRT(161), BRT(162), BRT(163), BRT(164), BRT(165), BRT(166), BRT(167), BRT(168), BRT(169), BRT(170),
	BRT(171), BRT(172), BRT(173), BRT(174), BRT(175), BRT(176), BRT(177), BRT(178), BRT(179), BRT(180),
	BRT(181), BRT(182), BRT(183), BRT(184), BRT(185), BRT(186), BRT(187), BRT(188), BRT(189), BRT(190),
	BRT(191), BRT(192), BRT(193), BRT(194), BRT(195), BRT(196), BRT(197), BRT(198), BRT(199), BRT(200),
	BRT(201), BRT(202), BRT(203), BRT(204), BRT(205), BRT(206), BRT(207), BRT(208), BRT(209), BRT(210),
	BRT(211), BRT(212), BRT(213), BRT(214), BRT(215), BRT(216), BRT(217), BRT(218), BRT(219), BRT(220),
	BRT(221), BRT(222), BRT(223), BRT(224), BRT(225), BRT(226), BRT(227), BRT(228), BRT(229), BRT(230),
	BRT(231), BRT(232), BRT(233), BRT(234), BRT(235), BRT(236), BRT(237), BRT(238), BRT(239), BRT(240),
	BRT(241), BRT(242), BRT(243), BRT(244), BRT(245), BRT(246), BRT(247), BRT(248), BRT(249), BRT(250),
	BRT(251), BRT(252), BRT(253), BRT(254), BRT(255),
	BRT(256), BRT(257), BRT(258), BRT(259), BRT(260), BRT(261), BRT(262), BRT(263), BRT(264), BRT(265),
	BRT(266), BRT(267), BRT(268), BRT(269), BRT(270), BRT(271), BRT(272), BRT(273), BRT(274), BRT(275),
	BRT(276), BRT(277), BRT(278), BRT(279), BRT(280), BRT(281), BRT(282), BRT(283), BRT(284), BRT(285),
	BRT(286), BRT(287), BRT(288), BRT(289), BRT(290), BRT(291), BRT(292), BRT(293), BRT(294), BRT(295),
	BRT(296), BRT(297), BRT(298), BRT(299), BRT(300), BRT(301), BRT(302), BRT(303), BRT(304), BRT(305),
	BRT(306), BRT(307), BRT(308), BRT(309), BRT(310), BRT(311), BRT(312), BRT(313), BRT(314), BRT(315),
	BRT(316), BRT(317), BRT(318), BRT(319), BRT(320), BRT(321), BRT(322), BRT(323), BRT(324), BRT(325),
	BRT(326), BRT(327), BRT(328), BRT(329), BRT(330), BRT(331), BRT(332), BRT(333), BRT(334), BRT(335),
	BRT(336), BRT(337), BRT(338), BRT(339), BRT(340), BRT(341), BRT(342), BRT(343), BRT(344), BRT(345),
	BRT(346), BRT(347), BRT(348), BRT(349), BRT(350), BRT(351), BRT(352), BRT(353), BRT(354), BRT(355),
	BRT(356), BRT(357), BRT(358), BRT(359), BRT(360), BRT(361), BRT(362), BRT(363), BRT(364), BRT(365),
	BRT(366), BRT(367), BRT(368), BRT(369), BRT(370), BRT(371), BRT(372), BRT(373), BRT(374), BRT(375),
	BRT(376), BRT(377), BRT(378), BRT(379), BRT(380), BRT(381), BRT(382), BRT(383), BRT(384), BRT(385),
	BRT(386), BRT(387), BRT(388), BRT(389), BRT(390), BRT(391), BRT(392), BRT(393), BRT(394), BRT(395),
	BRT(396), BRT(397), BRT(398), BRT(399), BRT(400), BRT(401), BRT(402), BRT(403), BRT(404), BRT(405),
	BRT(406), BRT(407), BRT(408), BRT(409), BRT(410), BRT(411), BRT(412), BRT(413), BRT(414), BRT(415),
	BRT(416), BRT(417), BRT(418), BRT(419), BRT(420), BRT(421), BRT(422), BRT(423), BRT(424), BRT(425),
	BRT(426), BRT(427), BRT(428), BRT(429), BRT(430), BRT(431), BRT(432), BRT(433), BRT(434), BRT(435),
	BRT(436), BRT(437), BRT(438), BRT(439), BRT(440), BRT(441), BRT(442), BRT(443), BRT(444), BRT(445),
	BRT(446), BRT(447), BRT(448), BRT(449), BRT(450), BRT(451), BRT(452), BRT(453), BRT(454), BRT(455),
	BRT(456), BRT(457), BRT(458), BRT(459), BRT(460),
	/* HBM+ */
	BRT(461), BRT(462), BRT(463), BRT(464), BRT(465),
	BRT(466), BRT(467), BRT(468), BRT(469), BRT(470), BRT(471), BRT(472), BRT(473), BRT(474), BRT(475),
	BRT(476), BRT(477), BRT(478), BRT(479), BRT(480), BRT(481), BRT(482), BRT(483), BRT(484), BRT(485),
	BRT(486), BRT(487), BRT(488), BRT(489), BRT(490), BRT(491), BRT(492), BRT(493), BRT(494), BRT(495),
	BRT(496), BRT(497), BRT(498), BRT(499), BRT(500), BRT(501), BRT(502), BRT(503), BRT(504), BRT(505),
	BRT(506), BRT(507), BRT(508), BRT(509), BRT(510), BRT(511), BRT(512), BRT(513), BRT(514), BRT(515),
	BRT(516), BRT(517), BRT(518), BRT(519), BRT(520), BRT(521), BRT(522), BRT(523), BRT(524), BRT(525),
	BRT(526), BRT(527), BRT(528), BRT(529), BRT(530), BRT(531), BRT(532), BRT(533), BRT(534), BRT(535),
	BRT(536), BRT(537), BRT(538), BRT(539), BRT(540), BRT(541), BRT(542), BRT(543), BRT(544), BRT(545),
	BRT(546), BRT(547), BRT(548), BRT(549), BRT(550), BRT(551), BRT(552), BRT(553), BRT(554), BRT(555),
	BRT(556), BRT(557), BRT(558), BRT(559), BRT(560), BRT(561), BRT(562), BRT(563), BRT(564), BRT(565),
	BRT(566), BRT(567), BRT(568), BRT(569), BRT(570), BRT(571), BRT(572), BRT(573), BRT(574), BRT(575),
	BRT(576), BRT(577), BRT(578), BRT(579), BRT(580), BRT(581), BRT(582), BRT(583), BRT(584), BRT(585),
	BRT(586), BRT(587), BRT(588), BRT(589), BRT(590), BRT(591), BRT(592), BRT(593), BRT(594), BRT(595),
	BRT(596), BRT(597), BRT(598), BRT(599), BRT(600), BRT(601), BRT(602), BRT(603), BRT(604), BRT(605),
	BRT(606), BRT(607), BRT(608), BRT(609), BRT(610), BRT(611), BRT(612), BRT(613), BRT(614), BRT(615),
	BRT(616), BRT(617), BRT(618), BRT(619), BRT(620), BRT(621), BRT(622), BRT(623), BRT(624), BRT(625),
	BRT(626), BRT(627), BRT(628), BRT(629), BRT(630), BRT(631), BRT(632), BRT(633), BRT(634), BRT(635),
	BRT(636), BRT(637), BRT(638), BRT(639), BRT(640), BRT(641), BRT(642), BRT(643), BRT(644), BRT(645),
	BRT(646), BRT(647), BRT(648), BRT(649), BRT(650), BRT(651), BRT(652), BRT(653), BRT(654), BRT(655),
	BRT(656), BRT(657), BRT(658), BRT(659), BRT(660), BRT(661), BRT(662), BRT(663), BRT(664), BRT(665),
};

static unsigned int a26x_lum_tbl[DB7F2010B_A26X_TOTAL_NR_LUMINANCE] = {
	/* normal 10x25 + 6 */
	2, 2, 2, 3, 3, 4, 5, 5, 6, 7,
	8, 9, 9, 10, 11, 12, 13, 14, 15, 16,
	17, 18, 19, 20, 21, 22, 23, 24, 25, 26,
	27, 29, 30, 31, 32, 33, 34, 35, 37, 38,
	39, 40, 42, 43, 44, 45, 47, 48, 49, 50,
	52, 53, 54, 56, 57, 58, 60, 61, 62, 64,
	65, 67, 68, 69, 71, 72, 74, 75, 76, 78,
	79, 81, 82, 84, 85, 87, 88, 90, 91, 93,
	94, 96, 97, 99, 100, 102, 103, 105, 106, 108,
	109, 111, 113, 114, 116, 117, 119, 120, 122, 124,
	125, 127, 129, 130, 132, 133, 135, 137, 138, 140,
	142, 143, 145, 147, 148, 150, 152, 153, 155, 157,
	158, 160, 162, 164, 165, 167, 169, 170, 172, 174,
	176, 177, 179, 181, 183, 184, 186, 188, 190, 191,
	193, 195, 197, 199, 200, 202, 204, 206, 208, 209,
	211, 213, 215, 217, 218, 220, 222, 224, 226, 228,
	230, 231, 233, 235, 237, 239, 241, 243, 244, 246,
	248, 250, 252, 254, 256, 258, 260, 262, 263, 265,
	267, 269, 271, 273, 275, 277, 279, 281, 283, 285,
	287, 289, 291, 293, 294, 296, 298, 300, 302, 304,
	306, 308, 310, 312, 314, 316, 318, 320, 322, 324,
	326, 328, 330, 332, 334, 336, 338, 340, 342, 344,
	347, 349, 351, 353, 355, 357, 359, 361, 363, 365,
	367, 369, 371, 373, 375, 377, 379, 382, 384, 386,
	388, 390, 392, 394, 396, 398, 400, 403, 405, 407,
	409, 411, 413, 415, 417, 420,
	/* hbm 10x20 + 5 = 205 */
	445, 446, 448, 450, 451, 453, 455, 457, 458, 460,
	462, 464, 465, 467, 469, 471, 472, 474, 476, 478,
	479, 481, 483, 485, 486, 488, 490, 492, 493, 495,
	497, 499, 500, 502, 504, 506, 507, 509, 511, 513,
	514, 516, 518, 520, 521, 523, 525, 527, 528, 530,
	532, 534, 535, 537, 539, 540, 542, 544, 546, 547,
	549, 551, 553, 554, 556, 558, 560, 561, 563, 565,
	567, 568, 570, 572, 574, 575, 577, 579, 581, 582,
	584, 586, 588, 589, 591, 593, 595, 596, 598, 600,
	602, 603, 605, 607, 609, 610, 612, 614, 616, 617,
	619, 621, 623, 624, 626, 628, 629, 631, 633, 635,
	636, 638, 640, 642, 643, 645, 647, 649, 650, 652,
	654, 656, 657, 659, 661, 663, 664, 666, 668, 670,
	671, 673, 675, 677, 678, 680, 682, 684, 685, 687,
	689, 691, 692, 694, 696, 698, 699, 701, 703, 705,
	706, 708, 710, 712, 713, 715, 717, 718, 720, 722,
	724, 725, 727, 729, 731, 732, 734, 736, 738, 739,
	741, 743, 745, 746, 748, 750, 752, 753, 755, 757,
	759, 760, 762, 764, 766, 767, 769, 771, 773, 774,
	776, 778, 780, 781, 783, 785, 787, 788, 790, 792,
	794, 795, 797, 799, 800,
	/* hbm+ 10x10 + 5 = 205 */
	801, 802, 804, 805, 806, 807, 809, 810, 811, 812,
	813, 815, 816, 817, 818, 820, 821, 822, 823, 824,
	826, 827, 828, 829, 830, 832, 833, 834, 835, 837,
	838, 839, 840, 841, 843, 844, 845, 846, 848, 849,
	850, 851, 852, 854, 855, 856, 857, 859, 860, 861,
	862, 863, 865, 866, 867, 868, 870, 871, 872, 873,
	874, 876, 877, 878, 879, 880, 882, 883, 884, 885,
	887, 888, 889, 890, 891, 893, 894, 895, 896, 898,
	899, 900, 901, 902, 904, 905, 906, 907, 909, 910,
	911, 912, 913, 915, 916, 917, 918, 920, 921, 922,
	923, 924, 926, 927, 928, 929, 930, 932, 933, 934,
	935, 937, 938, 939, 940, 941, 943, 944, 945, 946,
	948, 949, 950, 951, 952, 954, 955, 956, 957, 959,
	960, 961, 962, 963, 965, 966, 967, 968, 970, 971,
	972, 973, 974, 976, 977, 978, 979, 980, 982, 983,
	984, 985, 987, 988, 989, 990, 991, 993, 994, 995,
	996, 998, 999, 1000, 1001, 1002, 1004, 1005, 1006, 1007,
	1009, 1010, 1011, 1012, 1013, 1015, 1016, 1017, 1018, 1020,
	1021, 1022, 1023, 1024, 1026, 1027, 1028, 1029, 1030, 1032,
	1033, 1034, 1035, 1037, 1038, 1039, 1040, 1041, 1043, 1044,
	1045, 1046, 1048, 1049, 1050,
};

static unsigned int a26x_step_cnt_tbl[DB7F2010B_A26X_TOTAL_STEP] = {
	[0 ... DB7F2010B_A26X_TOTAL_STEP - 1] = 1,
};

static struct brightness_table db7f2010b_a26x_panel_brightness_table = {
	.control_type = BRIGHTNESS_CONTROL_TYPE_GAMMA_MODE2,
	.brt = a26x_brt_tbl,
	.sz_brt = ARRAY_SIZE(a26x_brt_tbl),
	.sz_ui_brt = DB7F2010B_A26X_NR_STEP,
	.sz_hbm_brt = DB7F2010B_A26X_HBM_STEP,
	.lum = a26x_lum_tbl,
	.sz_lum = DB7F2010B_A26X_TOTAL_NR_LUMINANCE,
	.sz_ui_lum = DB7F2010B_A26X_NR_LUMINANCE,
	.sz_hbm_lum = DB7F2010B_A26X_NR_HBM_LUMINANCE,
	.sz_ext_hbm_lum = 0,
	.brt_to_step = NULL,
	.sz_brt_to_step = 0,
	.step_cnt = a26x_step_cnt_tbl,
	.sz_step_cnt = ARRAY_SIZE(a26x_step_cnt_tbl),
	.vtotal = 0,
};

static struct panel_dimming_info db7f2010b_a26x_panel_dimming_info = {
	.name = "db7f2010b_a26x",
	.dim_init_info = {
		NULL,
	},
	.target_luminance = -1,
	.nr_luminance = 0,
	.hbm_target_luminance = -1,
	.nr_hbm_luminance = 0,
	.extend_hbm_target_luminance = -1,
	.nr_extend_hbm_luminance = 0,
	.brt_tbl = &db7f2010b_a26x_panel_brightness_table,
	/* dimming parameters */
	.dimming_maptbl = NULL,
	.dim_flash_on = false,
	.hbm_aor = NULL,
};
#endif /* __DB7F2010B_PANEL_DIMMING_H___ */
