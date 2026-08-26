/****************************************************************************
 * apps/vendor/bouffalolab/boards/bl616cl/ai-m64l-32s-kit/src/ai_m64l_kit.h
 *
 * Licensed to the Apache Software Foundation (ASF) under one or more
 * contributor license agreements.  See the NOTICE file distributed with
 * this work for additional information regarding copyright ownership.  The
 * ASF licenses this file to you under the Apache License, Version 2.0 (the
 * "License"); you may not use this file except in compliance with the
 * License.  You may obtain a copy of the License at
 *
 *   http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS, WITHOUT
 * WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.  See the
 * License for the specific language governing permissions and limitations
 * under the License.
 *
 ****************************************************************************/

#ifndef __VENDOR_BOUFFALOLAB_BOARDS_BL616CL_AI_M64L_KIT_AI_M64L_KIT_H
#define __VENDOR_BOUFFALOLAB_BOARDS_BL616CL_AI_M64L_KIT_AI_M64L_KIT_H

/****************************************************************************
 * Public Function Prototypes
 ****************************************************************************/

int ai_m64l_kit_bringup(void);

#ifdef CONFIG_BL616CL_GPIO
int ai_m64l_kit_gpio_initialize(void);
#endif

#endif /* __VENDOR_BOUFFALOLAB_BOARDS_BL616CL_AI_M64L_KIT_AI_M64L_KIT_H */
