/*
 * Basic Clock Tree Building Blocks
 *
 * Copyright (C) 2012 Andre Beckus
 *
 * Source code roughly based on omap_clk.c
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 of
 * the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, see <http://www.gnu.org/licenses/>.
 */

#include "hw/hw.h"
#include "hw/arm/stm32_clktree.h"


/* DEFINITIONS*/

/* See README for DEBUG documentation */
//#define DEBUG_CLKTREE


/* Helper to:
 *  1) Add a value to an array.
 *  2) Increment the array size count.
 *  3) Make sure the array does not overflow.
 */
#define CLKTREE_ADD_LINK(array, count, value, array_size) \
            (array)[(count)++] = (value); \
            assert((count) <= (array_size));


struct Stm32Clk {
    const char *name;

    bool enabled;

    uint32_t input_freq, output_freq, max_output_freq;

    uint16_t multiplier, divisor;

    unsigned user_count;
    qemu_irq user[CLKTREE_MAX_IRQ]; /* Who to notify on change */

    unsigned output_count;
    struct Stm32Clk *output[CLKTREE_MAX_OUTPUT];

    unsigned input_count;
    int selected_input;
    struct Stm32Clk *input[CLKTREE_MAX_INPUT];
};

static void stm32clktree_recalc_output_freq(Stm32Clk clk);




/* HELPER FUNCTIONS */

static Stm32Clk stm32clktree_get_input_clk(Stm32Clk clk)
{
    return clk->input[clk->selected_input + 1];
}

#ifdef DEBUG_CLKTREE

static void stm32clktree_print_state(Stm32Clk clk)
{
    Stm32Clk input_clk = stm32clktree_get_input_clk(clk);

    printf("STM32_CLKTREE: %s Output Change (SrcStm32Clk:%s InFreq:%lu OutFreq:%lu Mul:%u Div:%u Enabled:%c)\n",
            clk->name,
            input_clk ? input_clk->name : "None",
            (unsigned long)clk->input_freq,
            (unsigned long)clk->output_freq,
            (unsigned)clk->multiplier,
            (unsigned)clk->divisor,
            clk->enabled ? '1' : '0');
}
#endif

static void stm32clktree_set_input_freq(Stm32Clk clk, uint32_t input_freq)
{
    clk->input_freq = input_freq;

    stm32clktree_recalc_output_freq(clk);
}

/* Recalculates the output frequency based on the clock's input_freq variable.
 */
static void stm32clktree_recalc_output_freq(Stm32Clk clk) {
    int i;
    Stm32Clk next_clk, next_clk_input;
    uint32_t new_output_freq;

    /* Get the output frequency, or 0 if the output is disabled. */
    new_output_freq = clk->enabled ?
                            muldiv64(clk->input_freq,
                                     clk->multiplier,
                                     clk->divisor)
                            : 0;

    /* if the frequency has changed. */
   if(new_output_freq != clk->output_freq) {
       clk->output_freq = new_output_freq;

#ifdef DEBUG_CLKTREE
        stm32clktree_print_state(clk);
#endif

        /* Check the new frequency against the max frequency. */
        if(new_output_freq > clk->max_output_freq) {
            fprintf(stderr, "%s: Clock %s output frequency (%d Hz) exceeds max frequency (%d Hz).\n",
                    __FUNCTION__,
                    clk->name,
                    new_output_freq,
                    clk->max_output_freq);
        }

        /* Notify users of change. */
        for(i=0; i < clk->user_count; i++) {
            qemu_set_irq(clk->user[i], 1);
        }

        /* Propagate the frequency change to the child clocks */
        for(i=0; i < clk->output_count; i++) {
            next_clk = clk->output[i];
            assert(next_clk != NULL);

            /* Only propagate the change if the child has selected the current
             * clock as input.
             */
            next_clk_input = stm32clktree_get_input_clk(next_clk);
            if(next_clk_input == clk) {
                /* Recursively propagate changes.  The clock tree should not be
                 * too deep, so we shouldn't have to recurse too many times.
                 */
                stm32clktree_set_input_freq(next_clk, new_output_freq);
            }
        }
    }
}


/* Generic create routine used by the public create routines. */
static Stm32Clk stm32clktree_create_generic(
                    const char *name,
                    uint16_t multiplier,
                    uint16_t divisor,
                    bool enabled)
{
    Stm32Clk clk = (Stm32Clk)g_malloc(sizeof(struct Stm32Clk));

    clk->name = name;

    clk->input_freq = 0;
    clk->output_freq = 0;
    clk->max_output_freq = CLKTREE_NO_MAX_FREQ;

    clk->multiplier = multiplier;
    clk->divisor = divisor;

    clk->enabled = enabled;

    clk->user_count = 0;

    clk->output_count = 0;

    clk->input_count = 1;
    clk->input[0] = NULL;
    clk->selected_input = CLKTREE_NO_INPUT;

    return clk;
}









/* PUBLIC FUNCTIONS */
bool stm32clktree_is_enabled(Stm32Clk clk)
{
    return clk->enabled;
}

uint32_t stm32clktree_get_output_freq(Stm32Clk clk)
{
    return clk->output_freq;
}

void stm32clktree_adduser(Stm32Clk clk, qemu_irq user)
{
    CLKTREE_ADD_LINK(
            clk->user,
            clk->user_count,
            user,
            CLKTREE_MAX_IRQ);
}


Stm32Clk stm32clktree_create_src_clk(
                    const char *name,
                    uint32_t src_freq,
                    bool enabled)
{
    Stm32Clk clk;

    clk = stm32clktree_create_generic(name, 1, 1, enabled);

    stm32clktree_set_input_freq(clk, src_freq);

    return clk;
}


Stm32Clk stm32clktree_create_clk(
                    const char *name,
                    uint16_t multiplier,
                    uint16_t divisor,
                    bool enabled,
                    uint32_t max_output_freq,
                    int selected_input,
                    ...)
{
    va_list input_clks;
    Stm32Clk clk, input_clk;

    clk = stm32clktree_create_generic(name, multiplier, divisor, enabled);

    /* Add the input clock connections. */
    va_start(input_clks, selected_input);
    while((input_clk = va_arg(input_clks, Stm32Clk)) != NULL) {
        CLKTREE_ADD_LINK(
                clk->input,
                clk->input_count,
                input_clk,
                CLKTREE_MAX_INPUT);

        CLKTREE_ADD_LINK(
                input_clk->output,
                input_clk->output_count,
                clk,
                CLKTREE_MAX_OUTPUT);
    }

    stm32clktree_set_selected_input(clk, selected_input);

    return clk;
}


void stm32clktree_set_scale(Stm32Clk clk, uint16_t multiplier, uint16_t divisor)
{
    clk->multiplier = multiplier;
    clk->divisor = divisor;

    stm32clktree_recalc_output_freq(clk);
}


void stm32clktree_set_enabled(Stm32Clk clk, bool enabled)
{
    clk->enabled = enabled;

    stm32clktree_recalc_output_freq(clk);
}


void stm32clktree_set_selected_input(Stm32Clk clk, int selected_input)
{
    uint32_t input_freq;

    assert((selected_input + 1) < clk->input_count);

    clk->selected_input = selected_input;

    /* Get the input clock frequency.  If there is no input, this should be 0.
     */
    if(selected_input > -1) {
        input_freq = stm32clktree_get_input_clk(clk)->output_freq;
    } else {
        input_freq = 0;
    }

    stm32clktree_set_input_freq(clk, input_freq);
}
