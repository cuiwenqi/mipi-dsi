// (c) Copyright(C) 2020 - 2021 by Xilinx, Inc. All rights reserved.
// SPDX-License-Identifier: MIT

#include <fstream>
#include <functional>
#include <string.h>
#include <vector>
#include <xaiengine.h>

#include <xaiefal/rsc/xaiefal-bc.hpp>
#include <xaiefal/rsc/xaiefal-rsc-base.hpp>

#pragma once

using namespace std;

namespace xaiefal {
namespace resource {
using namespace xaiefal::log;
	/**
	 * @class XAieTraceCntr
	 * @brief Trace Event resource class
	 */
	class XAieTraceCntr: public XAieRsc {
	public:
		XAieTraceCntr() = delete;
		XAieTraceCntr(std::shared_ptr<XAieDev> Dev, const XAie_LocType &L):
			XAieRsc(Dev), Loc(L), Pkt() {
			uint32_t TType = _XAie_GetTileTypefromLoc(Aie->dev(), Loc);

			if (TType == XAIEGBL_TILE_TYPE_MAX) {
				State.Initialized = 0;
			} else {
				if (TType == XAIEGBL_TILE_TYPE_SHIMPL ||
					TType == XAIEGBL_TILE_TYPE_SHIMNOC) {
					Mod = XAIE_PL_MOD;
				} else if (TType == XAIEGBL_TILE_TYPE_AIETILE) {
					Mod = XAIE_CORE_MOD;
				} else {
					Mod = XAIE_MEM_MOD;
				}
				XAie_EventPhysicalToLogicalConv(Aie->dev(), Loc, Mod, 0, &StartEvent);
				StopEvent = StartEvent;
				Mode = XAIE_TRACE_EVENT_TIME;
				TraceSlotBits = 0;
				State.Initialized = 1;
			}
		}
		XAieTraceCntr(std::shared_ptr<XAieDev> Dev, const XAie_LocType &L,
			XAie_ModuleType M):
			XAieTraceCntr(Dev, L) {
			Mod = M;
			if (_XAie_CheckModule(Aie->dev(), Loc, Mod) != XAIE_OK) {
				State.Initialized = 0;
				Logger::log(LogLevel::ERROR) << __func__ <<
					"Invalid tile and module." << endl;
			}
		}
		/**
		 * This funtion sets module of the Trace control.
		 *
		 * @param M module of the the trace control
		 * @return XAIE_OK for success, error code for failure
		 */
		AieRC setModule(XAie_ModuleType M) {
			AieRC RC;

			if (State.Reserved == 1) {
				Logger::log(LogLevel::ERROR) << __func__ <<
					"failed for trace control, already reserved." << endl;
				RC = XAIE_ERR;
			} else if (_XAie_CheckModule(Aie->dev(), Loc, Mod) != XAIE_OK) {
				Logger::log(LogLevel::ERROR) << __func__ <<
					"failed for trace control, invalid module for tile." << endl;
				RC = XAIE_INVALID_ARGS;
			} else {
				Mod = M;
				State.Initialized = 1;
				RC = XAIE_OK;
			}
			return RC;

		}
		/**
		 * This funtion gets module of the Trace control.
		 *
		 * @return module of the trace control
		 */
		XAie_ModuleType getModule() {
			return Mod;
		}
		/**
		 * This funtion reserve a trace slot.
		 * It will fail if the trace control is already configured in
		 * hardware. This function is supposed to be called before
		 * start().
		 *
		 * @param Slot to return the reserved trace slot.
		 * @return XAIE_OK for success, error code for failure
		 */
		AieRC reserveTraceSlot(uint8_t &Slot) {
			AieRC RC = XAIE_ERR;

			if (State.Running == 1) {
				Logger::log(LogLevel::ERROR) << __func__ <<
					"failed, tracing already started." << endl;
			} else {
				for (int i = 0; i < (int)sizeof(TraceSlotBits) * 8; i++) {
					uint8_t Mask = 1 << i;

					if ((TraceSlotBits & Mask) == 0) {
						TraceSlotBits |= Mask;
						Slot = i;
						RC = XAIE_OK;
						break;
					}
				}
			}
			return RC;
		}
		/**
		 * This funtion release a trace slot.
		 *
		 * @param Slot trace slot to release
		 * @return XAIE_OK for success, error code for failure
		 */
		AieRC releaseTraceSlot(uint8_t Slot) {
			AieRC RC = XAIE_ERR;

			if (State.Running == 1) {
				Logger::log(LogLevel::ERROR) << __func__ <<
					"failed, tracing already started." << endl;
			} else if (Slot >= (int)(sizeof(TraceSlotBits) * 8)) {
				Logger::log(LogLevel::ERROR) << __func__ <<
					"failed, invalid slot id " << Slot << "." << endl;
				RC = XAIE_INVALID_ARGS;
			} else {
				XAie_Events E;

				TraceSlotBits &= ~(1 << Slot);
				XAie_EventPhysicalToLogicalConv(Aie->dev(), Loc, Mod, 0, &E);
				Events[Slot] = E;
				RC = XAIE_OK;
			}
			return RC;
		}
		/**
		 * This function sets events to trace to this object.
		 * No hardware configuration is changed.
		 * It will check if trace control is already configured. If
		 * yes, it will fail the setting. It will also fail if the slot
		 * is not yet assigned. User is expected to call
		 * reserveTraceSlot() first before setTraceEvent().
		 *
		 * @param Slot trace slot for the event to configure
		 * @param E event to configure.
		 * @return XAIE_OK for success, error code for failure
		 */
		AieRC setTraceEvent(uint32_t Slot, XAie_Events E) {
			AieRC RC;

			Logger::log(LogLevel::DEBUG) << __func__ << " " <<
				"(" << (uint32_t)Loc.Col << "," << (uint32_t)Loc.Row << ") Mod=" << Mod <<
				" Slot=" << Slot << " E=" << E << endl;
			if (State.Initialized == 0) {
				Logger::log(LogLevel::ERROR) << __func__ <<
					"failed, trace cntr object not initialized, set module first." << endl;
				RC = XAIE_ERR;
			} else if (State.Running == 1) {
				Logger::log(LogLevel::ERROR) << __func__ <<
					"failed, trace started." << endl;
				RC = XAIE_ERR;
			} else if (Slot >= (int)sizeof(TraceSlotBits) * 8) {
				Logger::log(LogLevel::ERROR) << __func__ <<
					"failed, invalid slot." << endl;
				RC = XAIE_INVALID_ARGS;
			} else if ((TraceSlotBits & (1 << Slot)) == 0) {
				Logger::log(LogLevel::ERROR) << __func__ <<
					"failed, trace slot is not reserved." << endl;
				RC = XAIE_INVALID_ARGS;
			} else {
				if (E == XAIE_EVENT_NONE_CORE) {
					XAie_EventPhysicalToLogicalConv(Aie->dev(), Loc, Mod, 0, &E);
				}
				Events[Slot] = E;
				RC = XAIE_OK;
			}
			changeToConfigured();
			return RC;
		}
		/**
		 * This function sets start event of the trace control.
		 * No hardware configuration is changed.
		 * It will check if trace control is already configured. If yes,
		 * it will fail the setting. That is it needs to be called
		 * before start().
		 *
		 * @param StartE start event to set
		 * @param StopE stop event to set
		 * @return XAIE_OK for success, error code for failure
		 */
		AieRC setCntrEvent(XAie_Events StartE, XAie_Events StopE) {
			AieRC RC;

			Logger::log(LogLevel::DEBUG) << __func__ << " " <<
				"(" << (uint32_t)Loc.Col << "," << (uint32_t)Loc.Row << ") Mod=" << Mod <<
				" StartE=" << StartE << " StopE=" << StopE << endl;
			if (State.Initialized == 0) {
				Logger::log(LogLevel::ERROR) << __func__ <<
					"failed, trace cntr object not initialized, set module first." << endl;
				RC = XAIE_ERR;
			} else if (State.Running == 1) {
				Logger::log(LogLevel::ERROR) << __func__ <<
					"failed, trace started." << endl;
				RC = XAIE_ERR;
			} else {
				uint8_t HwE;

				RC = XAie_EventLogicalToPhysicalConv(Aie->dev(), Loc,
						Mod, StartE, &HwE);
				if (RC == XAIE_OK) {
					RC = XAie_EventLogicalToPhysicalConv(Aie->dev(), Loc,
						Mod, StopE, &HwE);
				}
			}
			if (RC == XAIE_OK) {
				StartEvent = StartE;
				StopEvent = StopE;
				changeToConfigured();
			}
			return RC;
		}
		/**
		 * This function sets trace control mode.
		 * This function needs to be called before trace control is
		 * configured in hardware. That is it needs to be called before
		 * start().
		 *
		 * @param M trace control mode
		 * @return XAIE_OK for success, error code for failure.
		 */
		AieRC setMode(XAie_TraceMode M) {
			AieRC RC;

			if (State.Initialized == 0) {
				Logger::log(LogLevel::ERROR) << __func__ <<
					"failed, trace cntr object not initialized, set module first." << endl;
				RC = XAIE_ERR;
			} else if (State.Running == 1) {
				Logger::log(LogLevel::ERROR) << __func__ <<
					"failed, trace started." << endl;
				RC = XAIE_ERR;
			} else {
				Mode = M;
				changeToConfigured();
				RC = XAIE_OK;
			}
			return RC;
		}
		/**
		 * This function sets trace control packet config.
		 * This function needs to be called before trace control is
		 * configured in hardware. That is it needs to be called before
		 * start().
		 *
		 * @param P trace control packet setting
		 * @return XAIE_OK for success, error code for failure.
		 */
		AieRC setPkt(const XAie_Packet &P) {
			AieRC RC;

			if (State.Initialized == 0) {
				Logger::log(LogLevel::ERROR) << __func__ <<
					"failed, trace cntr object not initialized, set module first." << endl;
				RC = XAIE_ERR;
			} else if (State.Running == 1) {
				Logger::log(LogLevel::ERROR) << __func__ <<
					"failed, trace started." << endl;
				RC = XAIE_ERR;
			} else {
				Pkt = P;
				changeToConfigured();
				RC = XAIE_OK;
			}
			return RC;
		}
		/**
		 * This functinon returns the max number of trace events
		 * supported by the trace control.
		 *
		 * @return supported max trace events
		 */
		uint32_t getMaxTraceEvents() const {
			// TODO: It is better to get from C driver.
			return 8;
		}
		/**
		 * This functinon returns the number of trace events
		 * which have been reserved.
		 *
		 * @return number of reserved events.
		 */
		uint32_t getReservedTraceEvents() const {
			uint32_t NumEvents = 0;
			uint8_t TmpSlots = TraceSlotBits;

			for (int i = 0; i < (int)sizeof(TraceSlotBits) * 8; i++) {
				if ((TmpSlots & (uint32_t)1) != 0) {
					NumEvents++;
				}
			}
			return NumEvents;
		}
		/**
		 * This function returns tile location.
		 *
		 * @return counter tile location
		 */
		const XAie_LocType &loc() const {
			return Loc;
		}
	protected:
		AieRC _reserve() {
			//TODO: check C driver to see if the trace control is
			// in use
			return XAIE_OK;
		}

		AieRC _release() {
			// TODO: release the trace module to the C driver
			return XAIE_OK;
		}

		AieRC _start() {
			AieRC RC;
			std::vector<XAie_Events> vE;
			std::vector<u8> vSlot;

			Logger::log(LogLevel::DEBUG) << "trace control " << __func__ << " (" <<
				(uint32_t)Loc.Col << "," << (uint32_t)Loc.Row << ") Mod=" << Mod << endl;
			for (int i = 0; i < (int)sizeof(TraceSlotBits) * 8; i++) {
				if ((TraceSlotBits & (1 << i)) != 0) {
					vE.push_back(Events[i]);
					vSlot.push_back((uint8_t)i);
				}
			}
			RC = XAie_TraceEventList(Aie->dev(), Loc, Mod, vE.data(), vSlot.data(),
				vE.size());
			if (RC == XAIE_OK) {
				RC = XAie_TracePktConfig(Aie->dev(), Loc, Mod, Pkt);
				if (RC == XAIE_OK) {
					RC = XAie_TraceControlConfig(Aie->dev(),
						Loc, Mod, StartEvent,
						StopEvent, Mode);
				}
			}
			return RC;
		}

		AieRC _stop() {
			AieRC RC;

			Logger::log(LogLevel::DEBUG) << "trace control " << __func__ << " (" <<
				(uint32_t)Loc.Col << "," << (uint32_t)Loc.Row << ") Mod=" << Mod << endl;
			// Do not reset the packet setting as it can
			// cause issues on outstanding contents in
			// the trace buffer.
			// Reset the start, stop, and the trace events only.
			RC = XAie_TraceControlConfigReset(Aie->dev(), Loc, Mod);
			if (RC == XAIE_OK) {
				for (uint8_t s = 0;
					s < (uint8_t)sizeof(TraceSlotBits) * 8;
					s++) {
					if ((TraceSlotBits & (1 << s)) != 0) {
						XAie_TraceEventReset(Aie->dev(), Loc,
							Mod, s);
					}
				}
			}
			return RC;
		}

		XAie_LocType Loc; /**< tile location */
		XAie_ModuleType Mod; /**< module */
		uint8_t TraceSlotBits; /**< trace slots bitmap */
		XAie_Events Events[8]; /**< events to trace */
		XAie_Events StartEvent; /**< trace control start event */
		XAie_Events StopEvent; /**< trace control stop event */
		XAie_Packet Pkt; /**< trace packet setup */
		XAie_TraceMode Mode; /**< trace operation mode */
	private:
		void changeToConfigured() {
			if (State.Configured == 0 &&
				StartEvent != XAIE_EVENT_NONE_CORE &&
				TraceSlotBits != 0) {
				for (int i = 0; i < (int)sizeof(TraceSlotBits) * 8; i++) {
					XAie_Events E;

					if ((TraceSlotBits & (1 << i)) == 0) {
						continue;
					}
					XAie_EventPhysicalToLogicalConv(Aie->dev(), Loc, Mod, 0, &E);
					if (Events[i] != E) {
						State.Configured = 1;
						break;
					}
				}
			}
		}
	};
	/**
	 * @class XAieTraceEvent
	 * @brief Trace event resource class.
	 *	  Each event to trace is presented as a trace event resource
	 *	  class.
	 */
	class XAieTraceEvent: public XAieSingleTileRsc {
	public:
		XAieTraceEvent() = delete;
		XAieTraceEvent(std::shared_ptr<XAieDev> Dev, const XAie_LocType &L):
			XAieSingleTileRsc(Dev, L), BC(Dev) {}
		~XAieTraceEvent() {}
		/**
		 * This function initializes the trace event by assigning the
		 * trace control object.
		 *
		 * @param TraceCPtr AI engine module trace control this trace
		 *	  event belong to. It includes which modules, and what
		 *	  is the trace operation mode.
		 * @return XAIE_OK for success, error code for failure.
		 */
		AieRC initialize(std::shared_ptr<XAieTraceCntr> TraceCPtr) {
			AieRC RC = XAIE_OK;

			if (State.Reserved == 0) {
				uint32_t TType = _XAie_GetTileTypefromLoc(Aie->dev(),
					Loc);
				XAie_Events E;

				if (TType == XAIEGBL_TILE_TYPE_MAX) {
					RC = XAIE_INVALID_ARGS;
				} else if (TType == XAIEGBL_TILE_TYPE_SHIMNOC ||
					TType == XAIEGBL_TILE_TYPE_SHIMPL) {
					E = XAIE_EVENT_NONE_PL;
					if (TraceCPtr->getModule() != XAIE_PL_MOD) {
						Logger::log(LogLevel::ERROR) << "trace event " << __func__ << " (" <<
							(uint32_t)Loc.Col << "," << (uint32_t)Loc.Row <<
							") trace control tile type mismatched." << endl;
						RC = XAIE_INVALID_ARGS;
					}
				} else {
					if (TraceCPtr->getModule() == XAIE_MEM_MOD) {
						E = XAIE_EVENT_NONE_MEM;
					}
				}
				if (RC == XAIE_OK) {
					Event = E;
					Mod = TraceCPtr->getModule();
					TraceCntr = std::move(TraceCPtr);
					State.Initialized = 1;
				}
			} else {
				RC = XAIE_ERR;
				Logger::log(LogLevel::ERROR) << "trace event " << __func__ << " (" <<
					(uint32_t)Loc.Col << "," << (uint32_t)Loc.Row <<
					") Event Mod=" << Mod << " already reserved." << endl;
			}
			return RC;
		}
		/**
		 * This function sets event to trace.
		 *
		 * @param M module of the event
		 * @param E event to trace
		 * @return XAIE_OK for success, error code for failure
		 */
		AieRC setEvent(XAie_ModuleType M, XAie_Events E) {
			AieRC RC;
			uint8_t HwEvent;

			RC = XAie_EventLogicalToPhysicalConv(Aie->dev(), Loc,
					M, E, &HwEvent);
			if (RC != XAIE_OK) {
				Logger::log(LogLevel::ERROR) << "trace event " << __func__ << " (" <<
					(uint32_t)Loc.Col << "," << (uint32_t)Loc.Row <<
					") Event Mod=" << M << " Event=" << E <<
					" invalid event" << endl;
				RC = XAIE_INVALID_ARGS;
			} else if (State.Running == 1) {
				RC = XAIE_ERR;
				Logger::log(LogLevel::ERROR) << "trace event " << __func__ << " (" <<
					(uint32_t)Loc.Col << "," << (uint32_t)Loc.Row <<
					") Event Mod=" << M << " Event=" << E <<
					" trace event already in used" << endl;
			} else if (State.Reserved == 1 && M != Mod) {
				RC = XAIE_INVALID_ARGS;
				Logger::log(LogLevel::ERROR) << "trace event " << __func__ << " (" <<
					(uint32_t)Loc.Col << "," << (uint32_t)Loc.Row <<
					") Event Mod=" << M << " Event=" << E <<
					" trace event already reserved, input event module is different to the one already set" << endl;
			} else {
				Event = E;
				Mod = M;
				State.Configured = 1;
			}
			return RC;
		}
		/**
		 * This function returns the event to trace and its module.
		 * If the event is not set, it will return failure.
		 *
		 * @param M returns the module of the event
		 * @param E returns the event to trace
		 * @return XAIE_OK for success, error code for failure.
		 */
		AieRC getEvent(XAie_ModuleType &M, XAie_Events &E) const {
			AieRC RC;

			if (State.Configured == 0) {
				Logger::log(LogLevel::ERROR) << "trace event " << __func__ << " (" <<
					(uint32_t)Loc.Col << "," << (uint32_t)Loc.Row <<
					") trace control Mod=" << TraceCntr->getModule() <<
					" Event Mod=" << Mod << " no event specified" << endl;
				RC = XAIE_ERR;
			} else {
				E = Event;
				M = Mod;
				RC = XAIE_OK;
			}
			return RC;
		}
	protected:
		AieRC _reserve() {
			AieRC RC;

			Logger::log(LogLevel::DEBUG) << "trace event " << __func__ << " (" <<
				(uint32_t)Loc.Col << "," << (uint32_t)Loc.Row <<
				") trace control Mod=" << TraceCntr->getModule() <<
				" Event Mod=" << Mod << endl;
			RC = TraceCntr->reserveTraceSlot(Slot);
			if (RC != XAIE_OK) {
				Logger::log(LogLevel::ERROR) << "trace event " << __func__ << " (" <<
					(uint32_t)Loc.Col << "," << (uint32_t)Loc.Row <<
					") trace control Mod=" << TraceCntr->getModule() <<
					" Event Mod=" << Mod << " no trace slot" << endl;
			} else if (Mod != TraceCntr->getModule()) {
				std::vector<XAie_LocType> vL;

				vL.push_back(Loc);
				BC.initialize(vL, XAIE_CORE_MOD, XAIE_MEM_MOD);
				RC = BC.reserve();
				if (RC != XAIE_OK) {
					Logger::log(LogLevel::ERROR) << "trace event " << __func__ << " (" <<
						(uint32_t)Loc.Col << "," << (uint32_t)Loc.Row <<
						") trace control Mod=" << TraceCntr->getModule() <<
						" Event Mod=" << Mod << " no broadcast event" << endl;
					TraceCntr->releaseTraceSlot(Slot);
				} else {
					Rsc.Mod = Mod;
					Rsc.RscId = Slot;
				}
			}
			return RC;
		}
		AieRC _release() {
			Logger::log(LogLevel::DEBUG) << "trace event " << __func__ << " (" <<
				(uint32_t)Loc.Col << "," << (uint32_t)Loc.Row <<
				") trace control Mod=" << TraceCntr->getModule() <<
				" Event Mod=" << Mod << "Event=" << Event << endl;
			TraceCntr->releaseTraceSlot(Slot);
			if (Mod != TraceCntr->getModule()) {
				BC.release();
			}
			return XAIE_OK;
		}
		AieRC _start() {
			AieRC RC;

			Logger::log(LogLevel::DEBUG) << "trace event " << __func__ << " (" <<
				(uint32_t)Loc.Col << "," << (uint32_t)Loc.Row <<
				") trace control Mod=" << TraceCntr->getModule() <<
				" Event Mod=" << Mod << "Event=" << Event << endl;
			if (Mod != TraceCntr->getModule()) {
				RC = XAie_EventBroadcast(Aie->dev(), Loc, Mod,
						BC.getBc(), Event);
				if (RC == XAIE_OK) {
					XAie_Events BcE;

					BC.getEvent(Loc, TraceCntr->getModule(), BcE);
					RC = TraceCntr->setTraceEvent(Slot, BcE);
				}
			} else {
				RC = TraceCntr->setTraceEvent(Slot, Event);
			}
			return RC;
		}
		AieRC _stop() {
			AieRC RC, lRC;
			XAie_Events E;

			Logger::log(LogLevel::DEBUG) << "trace event " << __func__ << " (" <<
				(uint32_t)Loc.Col << "," << (uint32_t)Loc.Row <<
				") trace control Mod=" << TraceCntr->getModule() <<
				" Event Mod=" << Mod << "Event=" << Event << endl;
			RC = XAIE_OK;
			XAie_EventPhysicalToLogicalConv(Aie->dev(), Loc,
					TraceCntr->getModule(), 0, &E);
			lRC = TraceCntr->setTraceEvent(Slot, E);
			if (lRC != XAIE_OK) {
				RC = lRC;
			}
			if (Mod != TraceCntr->getModule()) {
				lRC = BC.stop();
				if (lRC != XAIE_OK) {
					RC = lRC;
				}
			}
			return RC;
		}
	protected:
		std::shared_ptr<XAieTraceCntr> TraceCntr;
		XAie_Events Event; /**< event to trace */
		XAieBroadcast BC; /**< broadcast resource if need to broadcast event to tracer */
		uint8_t Slot; /**< trace slot */
	};
	/**
	 * @class XAieTracing
	 * @brief class of AI engine event tracing.
	 *	  An XAieTracing object contains AI engine trace control and
	 *	  events to trace.
	 */
	class XAieTracing: public XAieRsc {
	public:
		XAieTracing() = delete;
		XAieTracing(std::shared_ptr<XAieDev> Dev, const XAie_LocType &L):
			XAieRsc(Dev) {
			TraceCntr = std::make_shared<XAieTraceCntr>(Dev, L);
		}
		XAieTracing(std::shared_ptr<XAieDev> Dev, const XAie_LocType &L,
			XAie_ModuleType M): XAieTracing(Dev, L) {
			TraceCntr = std::make_shared<XAieTraceCntr>(Dev, L, M);
			if (TraceCntr->isInitialized()) {
				State.Initialized = 1;
			}
		}
		~XAieTracing() {
			Events.clear();
		}
		/**
		 * This funtion sets module of the Trace control.
		 *
		 * @param M module of the the trace control
		 * @return XAIE_OK for success, error code for failure
		 */
		AieRC setModule(XAie_ModuleType M) {
			AieRC RC;

			RC = TraceCntr->setModule(M);
			if (RC == XAIE_OK) {
				State.Initialized = 1;
			}
			return RC;

		}
		/**
		 * This function adds an event to trace.
		 * This function will needs to be called before the trace
		 * control is configured in hardware. That is it needs to
		 * be called before start().
		 *
		 * @param M module of the event
		 * @param E event to trace
		 * @return XAIE_OK for success, error code for failure.
		 */
		AieRC addEvent(XAie_ModuleType M, XAie_Events E) {
			AieRC RC;

			Logger::log(LogLevel::DEBUG) << "tracing " << __func__ << " ("
				<< (uint32_t)TraceCntr->loc().Col << "," << (uint32_t)TraceCntr->loc().Row <<
				") Mod=" << (uint32_t)M << " E=" << E << endl;
			if (Events.size() == TraceCntr->getMaxTraceEvents()) {
				Logger::log(LogLevel::ERROR) << __func__ <<
					"failed for tracing, exceeded max num of events." << endl;
				RC = XAIE_ERR;
			} else if (State.Running == 1) {
				Logger::log(LogLevel::ERROR) << __func__ <<
					"failed for tracing, resource reserved." << endl;
				RC = XAIE_ERR;
			} else {
				XAieTraceEvent TraceE(Aie, TraceCntr->loc());

				TraceE.initialize(TraceCntr);
				RC = TraceE.setEvent(M, E);
				if (RC != XAIE_OK) {
					Logger::log(LogLevel::ERROR) << __func__ <<
						"failed for tracing, failed to initialize event." << endl;
				} else if (State.Reserved == 1) {
					RC = TraceE.reserve();
					if (RC != XAIE_OK) {
						Logger::log(LogLevel::ERROR) << __func__ <<
							"failed for tracing, reserved new event failed." << endl;
					} else {
						RC = TraceE.start();
						if (RC != XAIE_OK) {
							TraceE.release();
						}
					}
				}
				if (RC == XAIE_OK) {
					Events.push_back(TraceE);
					changeToConfigured();
				}
			}
			return RC;
		}
		/**
		 * This function removes an event.
		 * It needs to be called before start().
		 *
		 * @param M module of the event
		 * @param E event to remove
		 * @return XAIE_OK for success, error code for failure
		 */
		AieRC removeEvent(XAie_ModuleType M, XAie_Events E) {
			AieRC RC;

			Logger::log(LogLevel::DEBUG) << "tracing " << __func__ << " ("
				<< (uint32_t)TraceCntr->loc().Col << "," << (uint32_t)TraceCntr->loc().Row <<
				") Mod=" << (uint32_t)M << " E=" << E << endl;
			if (State.Running == 1) {
				Logger::log(LogLevel::ERROR) << __func__ <<
					"failed for tracing, resource reserved." << endl;
				RC = XAIE_ERR;
			} else {
				RC = XAIE_INVALID_ARGS;
				for (int i = 0; i < (int)Events.size(); i++) {
					XAie_Events lE;
					XAie_ModuleType lM;

					Events[i].getEvent(lM, lE);
					if (lM == M && E == lE) {
						Events.erase(Events.begin() + i);
						RC = XAIE_OK;
						break;
					}
				}
				if (RC != XAIE_OK) {
					Logger::log(LogLevel::ERROR) << __func__ <<
						"failed for tracing, event doesn't exist." << endl;
				} else {
					changeToConfigured();
				}
			}
			return RC;
		}
		/**
		 * This function sets start event of the trace control.
		 * No hardware configuration is changed.
		 * It will check if trace control is already configured. If yes,
		 * it will fail the setting. That is it needs to be called
		 * before start().
		 *
		 * @param StartE start event to set
		 * @param StopE stop event to set
		 * @return XAIE_OK for success, error code for failure
		 */
		AieRC setCntrEvent(XAie_Events StartE, XAie_Events StopE) {
			AieRC RC;

			Logger::log(LogLevel::DEBUG) << "tracing " << __func__ << " ("
				<< (uint32_t)TraceCntr->loc().Col << "," << (uint32_t)TraceCntr->loc().Row <<
				") Mod=" << (uint32_t)TraceCntr->getModule() <<
				" StartE=" << StartE << " StopE=" << StopE << endl;
			RC = TraceCntr->setCntrEvent(StartE, StopE);
			if (RC == XAIE_OK) {
				changeToConfigured();
			}
			return RC;
		}
		/**
		 * This function sets trace control mode.
		 * This function needs to be called before trace control is
		 * configured in hardware. That is it needs to be called before
		 * start().
		 *
		 * @param M trace control mode
		 * @return XAIE_OK for success, error code for failure.
		 */
		AieRC setMode(XAie_TraceMode M) {
			AieRC RC;

			Logger::log(LogLevel::DEBUG) << "tracing " << __func__ << " ("
				<< (uint32_t)TraceCntr->loc().Col << "," << (uint32_t)TraceCntr->loc().Row <<
				") M=" << M << endl;
			RC = TraceCntr->setMode(M);
			if (RC == XAIE_OK) {
				changeToConfigured();
			}
			return RC;
		}
		/**
		 * This function sets trace control packet config.
		 * This function needs to be called before trace control is
		 * configured in hardware. That is it needs to be called before
		 * start().
		 *
		 * @param P trace control packet setting
		 * @return XAIE_OK for success, error code for failure.
		 */
		AieRC setPkt(const XAie_Packet &P) {
			AieRC RC;

			Logger::log(LogLevel::DEBUG) << "tracing " << __func__ << " ("
				<< (uint32_t)TraceCntr->loc().Col << "," << (uint32_t)TraceCntr->loc().Row <<
				") Mod=" << (uint32_t)TraceCntr->getModule() << endl;
			RC = TraceCntr->setPkt(P);
			if (RC == XAIE_OK) {
				changeToConfigured();
			}
			return RC;
		}
		/**
		 * This functinon returns the number of available free trace
		 * slots on the trace control.
		 *
		 * @return number of reserved events.
		 */
		uint32_t getTraceControlAvailTraceSlots() const {
			uint32_t MaxEvents, NumReserved;

			MaxEvents = getMaxTraceEvents();
			NumReserved = TraceCntr->getReservedTraceEvents();
			return (MaxEvents - NumReserved);
		}
		/**
		 * This functinon returns the max number of trace events
		 * supported by the trace control.
		 *
		 * @return supported max trace events
		 */
		uint32_t getMaxTraceEvents() const {
			return TraceCntr->getMaxTraceEvents();
		}
		/**
		 * This function returns tile location
		 *
		 * @return counter tile location
		 */
		const XAie_LocType &loc() const {
			return TraceCntr->loc();
		}
	protected:
		AieRC _reserve() {
			AieRC RC = XAIE_OK;

			Logger::log(LogLevel::DEBUG) << "tracing " << __func__ << " ("
				<< (uint32_t)TraceCntr->loc().Col << "," << (uint32_t)TraceCntr->loc().Row <<
				") Mod=" << (uint32_t)TraceCntr->getModule() << endl;
			if (RC == XAIE_OK) {
				RC = TraceCntr->reserve();
			}
			for (int i = 0; i < (int)Events.size(); i++) {
				RC = Events[i].reserve();
				if (RC != XAIE_OK) {
					break;
				}
			}
			if (RC != XAIE_OK) {
				for (int i = 0; i < (int)Events.size(); i++) {
					Events[i].release();
				}
			} else {
				// After reserved tracing events, configure
				// them so that the trace control state can
				// change to configured.
				for (int i = 0; i < (int)Events.size(); i++) {
					Events[i].start();
				}
				changeToConfigured();
			}
			return RC;
		}
		AieRC _release() {
			Logger::log(LogLevel::DEBUG) << "tracing " << __func__ << " ("
				<< (uint32_t)TraceCntr->loc().Col << "," << (uint32_t)TraceCntr->loc().Row <<
				") Mod=" << (uint32_t)TraceCntr->getModule() << endl;
			TraceCntr->release();
			for (int i = 0; i < (int)Events.size(); i++) {
				Events[i].release();
			}
			return XAIE_OK;
		}
		AieRC _start() {
			AieRC RC = XAIE_OK;

			Logger::log(LogLevel::DEBUG) << "tracing " << __func__ << " (" <<
				(uint32_t)TraceCntr->loc().Col << "," << (uint32_t)TraceCntr->loc().Row << ")" <<
				" Mod=" << TraceCntr->getModule() << ", " << Events.size() << " events to trace." << endl;
			for (int i = 0; i < (int)Events.size(); i++) {
				RC = Events[i].start();
				if (RC != XAIE_OK) {
					break;
				}
			}
			if (RC == XAIE_OK) {
				RC = TraceCntr->start();
			}
			if (RC != XAIE_OK) {
				for (int i = 0; i < (int)Events.size(); i++) {
					Events[i].stop();
				}
			}
			return RC;
		}
		AieRC _stop() {
			Logger::log(LogLevel::DEBUG) << "tracing "<< __func__ << " (" <<
				(uint32_t)TraceCntr->loc().Col << "," << (uint32_t)TraceCntr->loc().Row << ")" <<
				" Mod=" << TraceCntr->getModule() << ", " << Events.size() << " events to trace." << endl;
			TraceCntr->stop();
			for (int i = 0; i < (int)Events.size(); i++) {
				Events[i].stop();
			}
			return XAIE_OK;
		}
		std::shared_ptr<XAieTraceCntr> TraceCntr;
		std::vector<XAieTraceEvent> Events;
	private:
		void changeToConfigured() {
			if (State.Configured == 0) {
				if (TraceCntr->isConfigured()) {
					if (Events.size() > 0) {
						State.Configured = 1;
					}
				}
			}
		}
	};
}
}