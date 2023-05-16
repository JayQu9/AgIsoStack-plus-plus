//================================================================================================
/// @file can_hardware_interface.hpp
///
/// @brief The hardware abstraction layer that separates the stack from the underlying CAN driver
/// @author Adrian Del Grosso
///
/// @copyright 2022 Adrian Del Grosso
//================================================================================================
#ifndef CAN_HARDWARE_INTERFACE_HPP
#define CAN_HARDWARE_INTERFACE_HPP

#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <cstring>
#include <deque>
#include <mutex>
#include <thread>
#include <vector>

#include "isobus/hardware_integration/can_hardware_plugin.hpp"
#include "isobus/isobus/can_message_frame.hpp"
#include "isobus/isobus/can_hardware_abstraction.hpp"
#include "isobus/utility/event_dispatcher.hpp"

namespace isobus
{
	//================================================================================================
	/// @class CANHardwareInterface
	///
	/// @brief Provides a common queuing and thread layer for running the CAN stack and all CAN drivers
	///
	/// @details The `CANHardwareInterface` class was created to provide a common queuing and thread
	/// layer for running the CAN stack and all CAN drivers to simplify integration and crucially to
	/// provide a consistent, safe order of operations for all the function calls needed to properly
	/// drive the stack.
	//================================================================================================
	class CANHardwareInterface
	{
	public:
		/// @brief Returns the number of configured CAN channels that the class is managing
		/// @returns The number of configured CAN channels that the class is managing
		static std::uint8_t get_number_of_can_channels();

		/// @brief Sets the number of CAN channels to manage
		/// @details Allocates the proper number of `CanHardware` objects to track
		/// each CAN channel's Tx and Rx message queues. If you pass in a smaller number than what was
		/// already configured, it will delete the unneeded `CanHardware` objects.
		/// @note The function will fail if the channel is already assigned to a driver or the interface is already started
		/// @param value The number of CAN channels to manage
		/// @returns `true` if the channel count was set, otherwise `false`.
		static bool set_number_of_can_channels(std::uint8_t value);

		/// @brief Assigns a CAN driver to a channel
		/// @param[in] channelIndex The channel to assign to
		/// @param[in] canDriver The driver to assign to the channel
		/// @note The function will fail if the channel is already assigned to a driver or the interface is already started
		/// @returns `true` if the driver was assigned to the channel, otherwise `false`
		static bool assign_can_channel_frame_handler(std::uint8_t channelIndex, std::shared_ptr<CANHardwarePlugin> canDriver);

		/// @brief Removes a CAN driver from a channel
		/// @param[in] channelIndex The channel to remove the driver from
		/// @note The function will fail if the channel is already assigned to a driver or the interface is already started
		/// @returns `true` if the driver was removed from the channel, otherwise `false`
		static bool unassign_can_channel_frame_handler(std::uint8_t channelIndex);

		/// @brief Starts the threads for managing the CAN stack and CAN drivers
		/// @returns `true` if the threads were started, otherwise false (perhaps they are already running)
		static bool start();

		/// @brief Stops all CAN management threads and discards all remaining messages in the Tx and Rx queues.
		/// @returns `true` if the threads were stopped, otherwise `false`
		static bool stop();

		/// @brief Checks if the CAN stack and CAN drivers are running
		/// @returns `true` if the threads are running, otherwise `false`
		static bool is_running();

		/// @brief Called externally, adds a message to a CAN channel's Tx queue
		/// @param[in] packet The packet to add to the Tx queue
		/// @returns `true` if the packet was accepted, otherwise `false` (maybe wrong channel assigned)
		static bool transmit_can_message(const isobus::CANMessageFrame &packet);

		/// @brief Get the event dispatcher for when a CAN message frame is received from hardware event
		/// @returns The event dispatcher which can be used to register callbacks/listeners to
		static isobus::EventDispatcher<isobus::CANMessageFrame> &get_can_frame_received_event_dispatcher();

		/// @brief Get the event dispatcher for when a CAN message frame will be send to hardware event
		/// @returns The event dispatcher which can be used to register callbacks/listeners to
		static isobus::EventDispatcher<isobus::CANMessageFrame> &get_can_frame_transmitted_event_dispatcher();

		/// @brief Get the event dispatcher for when a periodic update is called
		/// @returns The event dispatcher which can be used to register callbacks/listeners to
		static isobus::EventDispatcher<> &get_periodic_update_event_dispatcher();

		/// @brief Set the interval between periodic updates
		/// @param[in] value The interval between update calls in milliseconds
		static void set_periodic_update_interval(std::uint32_t value);

		/// @brief Get the interval between periodic updates
		/// @returns The interval between update calls in milliseconds
		static std::uint32_t get_periodic_update_interval();

	private:
		/// @brief Stores the Tx/Rx queues, mutexes, and driver needed to run a single CAN channel
		struct CANHardware
		{
			std::mutex messagesToBeTransmittedMutex; ///< Mutex to protect the Tx queue
			std::deque<isobus::CANMessageFrame> messagesToBeTransmitted; ///< Tx message queue for a CAN channel

			std::mutex receivedMessagesMutex; ///< Mutex to protect the Rx queue
			std::deque<isobus::CANMessageFrame> receivedMessages; ///< Rx message queue for a CAN channel

			std::unique_ptr<std::thread> receiveMessageThread; ///< Thread to manage getting messages from a CAN channel

			std::shared_ptr<CANHardwarePlugin> frameHandler; ///< The CAN driver to use for a CAN channel
		};

		/// @brief Singleton instance of the CANHardwareInterface class
		/// @details This is a little hack that allows to have the destructor called
		static CANHardwareInterface SINGLETON;

		/// @brief Constructor for the CANHardwareInterface class
		CANHardwareInterface() = default;

		/// @brief Deconstructor for the CANHardwareInterface class
		virtual ~CANHardwareInterface();

		/// @brief The default update interval for the CAN stack. Mostly arbitrary
		static constexpr std::uint32_t PERIODIC_UPDATE_INTERVAL = 4;

		/// @brief The main CAN thread executes this function. Does most of the work of this class
		static void update_thread_function();

		/// @brief The receive thread(s) execute this function
		/// @param[in] channelIndex The associated CAN channel for the thread
		static void receive_message_thread_function(std::uint8_t channelIndex);

		/// @brief Attempts to write a frame using the driver assigned to a packet's channel
		/// @param[in] packet The packet to try and write to the bus
		static bool transmit_can_message_from_buffer(isobus::CANMessageFrame &packet);

		/// @brief The periodic update thread executes this function
		static void periodic_update_function();

		/// @brief Stops all threads related to the hardware interface
		static void stop_threads();

		static std::unique_ptr<std::thread> updateThread; ///< The main thread
		static std::unique_ptr<std::thread> wakeupThread; ///< A thread that periodically wakes up the `updateThread`
		static std::condition_variable updateThreadWakeupCondition; ///< A condition variable to allow for signaling the `updateThread` to wakeup
		static std::atomic_bool stackNeedsUpdate; ///< Stores if the CAN thread needs to update the stack this iteration
		static std::uint32_t periodicUpdateInterval; ///< The period between calls to the CAN stack update function in milliseconds

		static isobus::EventDispatcher<isobus::CANMessageFrame> frameReceivedEventDispatcher; ///< The event dispatcher for when a CAN message frame is received from hardware event
		static isobus::EventDispatcher<isobus::CANMessageFrame> frameTransmittedEventDispatcher; ///< The event dispatcher for when a CAN message has been transmitted via hardware
		static isobus::EventDispatcher<> periodicUpdateEventDispatcher; ///< The event dispatcher for when a periodic update is called

		static std::vector<std::unique_ptr<CANHardware>> hardwareChannels; ///< A list of all CAN channel's metadata
		static std::mutex hardwareChannelsMutex; ///< Mutex to protect `hardwareChannels`
		static std::mutex updateMutex; ///< A mutex for the main thread
		static std::atomic_bool threadsStarted; ///< Stores if the threads have been started
	};
}
#endif // CAN_HARDWARE_INTERFACE_HPP
