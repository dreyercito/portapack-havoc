/*
 * Copyright (C) 2016 Jared Boone, ShareBrained Technology, Inc.
 *
 * This file is part of PortaPack.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2, or (at your option)
 * any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; see the file COPYING.  If not, write to
 * the Free Software Foundation, Inc., 51 Franklin Street,
 * Boston, MA 02110-1301, USA.
 */

#ifndef __AUDIO_THREAD_H__
#define __AUDIO_THREAD_H__

#include "ch.h"

#include "file.hpp"

#include "event_m0.hpp"

#include "portapack_shared_memory.hpp"

#include "hackrf_gpio.hpp"
using namespace hackrf::one;

#include <cstring>

class StreamOutput {
public:
	StreamOutput(
		FIFO<uint8_t>* const fifo
	) : fifo { fifo }
	{
	}

	size_t available() {
		return fifo->len();
	}

	size_t read(void* const data, const size_t length) {
		return fifo->out(reinterpret_cast<uint8_t*>(data), length);
	}

private:
	FIFO<uint8_t>* const fifo;
};

class AudioThread {
public:
	AudioThread(
		std::string file_path
	) : write_buffer { std::make_unique<std::array<uint8_t, write_size>>() }
	{
		if( !file.open_for_append(file_path) ) {
			return;
		}

		// Need significant stack for FATFS
		thread = chThdCreateFromHeap(NULL, 1024, NORMALPRIO + 10, AudioThread::static_fn, this);
	}

	~AudioThread() {
		chThdTerminate(thread);
		chEvtSignal(thread, EVT_FIFO_HIGHWATER);
		chThdWait(thread);
	}

	static void check_fifo_isr() {
		if( (shared_memory.FIFO_HACK != nullptr) && (thread != nullptr) ) {
			auto fifo = reinterpret_cast<FIFO<uint8_t>*>(shared_memory.FIFO_HACK);
			if( fifo->len() >= write_size ) {
				chEvtSignalI(thread, EVT_FIFO_HIGHWATER);
			}
		}
	}

private:
	static constexpr size_t write_size = 4096;
	static constexpr eventmask_t EVT_FIFO_HIGHWATER = 1;

	std::unique_ptr<std::array<uint8_t, write_size>> write_buffer;
	File file;
	static Thread* thread;

	static msg_t static_fn(void* arg) {
		auto obj = static_cast<AudioThread*>(arg);
		obj->run();

		return 0;
	}

	void run() {
		bool success = true;
		while( success && !chThdShouldTerminate() ) {
			chEvtWaitAny(EVT_FIFO_HIGHWATER);

			auto fifo = reinterpret_cast<FIFO<uint8_t>*>(shared_memory.FIFO_HACK);
			if( !fifo ) {
				break;
			}

			StreamOutput stream { fifo };

			while( success && (stream.available() >= write_buffer->size()) ) {
				success = transfer(stream, write_buffer.get());
			}
		}

		if( !success ) {
			led_tx.on();
		}
	}

	bool transfer(StreamOutput& stream, std::array<uint8_t, write_size>* const write_buffer) {
		bool success = false;

		led_usb.on();

		const auto bytes_to_write = stream.read(write_buffer->data(), write_buffer->size());
		if( bytes_to_write == write_buffer->size() ) {
			if( file.write(write_buffer->data(), write_buffer->size()) ) {
				success = true;
			}
		}

		led_usb.off();

		return success;
	}
};

#endif/*__AUDIO_THREAD_H__*/
