#include "ltme_node/ltme_node.h"

#include <arpa/inet.h>

#include "sensor_msgs/msg/laser_scan.hpp"

const std::string LidarDriver::DEFAULT_ENFORCED_TRANSPORT_MODE = "none";
const std::string LidarDriver::DEFAULT_FRAME_ID = "laser";
const int LidarDriver::DEFAULT_SCAN_FREQUENCY = 15;
const double LidarDriver::ANGLE_MIN_LIMIT = -2.356;
const double LidarDriver::ANGLE_MAX_LIMIT = 2.356;
const double LidarDriver::DEFAULT_ANGLE_EXCLUDED_MIN = -3.142;
const double LidarDriver::DEFAULT_ANGLE_EXCLUDED_MAX = -3.142;
const double LidarDriver::RANGE_MIN_LIMIT = 0.05;
const double LidarDriver::RANGE_MAX_LIMIT = 30;
const int LidarDriver::DEFAULT_AVERAGE_FACTOR = 1;

LidarDriver::LidarDriver(std::string name)
  : rclcpp::Node(name) {
  this->declare_parameter<std::string>("device_model", "LTME-02A");
  this->declare_parameter<std::string>("device_address", "192.168.10.160");
  try{
    rclcpp::Parameter device_model_param = this->get_parameter("device_model");
    device_model_ = device_model_param.as_string();
  }
  catch (...) {
    RCLCPP_ERROR(get_logger(),"Missing required parameter \"device_model\"");
    exit(-1);
  }
  try{
    rclcpp::Parameter device_address_param = this->get_parameter("device_address");
    device_address_ = device_address_param.as_string();
  }
  catch (...) {
    RCLCPP_ERROR(get_logger(),"Missing required parameter \"device_address\"");
    exit(-1);
  }

  enforced_transport_mode_ = this->declare_parameter<std::string>("enforced_transport_mode", DEFAULT_ENFORCED_TRANSPORT_MODE);
  frame_id_ = this->declare_parameter<std::string>("frame_id", DEFAULT_FRAME_ID);
  scan_frequency_override_ = this->declare_parameter<int>("scan_frequency_override", 0);
  angle_min_ = this->declare_parameter<double>("angle_min", ANGLE_MIN_LIMIT);
  angle_max_ = this->declare_parameter<double>("angle_max", ANGLE_MAX_LIMIT);
  angle_excluded_min_ = this->declare_parameter<double>("angle_excluded_min", DEFAULT_ANGLE_EXCLUDED_MIN);
  angle_excluded_max_ = this->declare_parameter<double>("angle_excluded_max", DEFAULT_ANGLE_EXCLUDED_MAX);
  range_min_ = this->declare_parameter<double>("range_min", RANGE_MIN_LIMIT);
  range_max_ = this->declare_parameter<double>("range_max", RANGE_MAX_LIMIT);
  average_factor_ = this->declare_parameter<int>("average_factor", DEFAULT_AVERAGE_FACTOR);

  frame_id_ = this->get_parameter("frame_id").as_string();
  angle_min_ = this->get_parameter("angle_min").as_double();
  angle_max_ = this->get_parameter("angle_max").as_double();
  range_min_ = this->get_parameter("range_min").as_double();
  range_max_ = this->get_parameter("range_max").as_double();

  std::cout<<device_model_<<std::endl
            <<device_address_<<std::endl
            <<frame_id_<<std::endl
            <<angle_min_<<std::endl
            <<angle_max_<<std::endl
            <<range_min_<<std::endl
            <<range_max_<<std::endl;


  if (!(enforced_transport_mode_ == "none" || enforced_transport_mode_ == "normal" || enforced_transport_mode_ == "oob")) {
    RCLCPP_ERROR(get_logger(),"Transport mode \"%s\" not supported", enforced_transport_mode_.c_str());
    exit(-1);
  }
  if (scan_frequency_override_ != 0 &&
    (scan_frequency_override_ < 10 || scan_frequency_override_ > 30 || scan_frequency_override_ % 5 != 0)) {
    RCLCPP_ERROR(get_logger(),"Scan frequency %d not supported", scan_frequency_override_);
    exit(-1);
  }
  if (!(angle_min_ < angle_max_)) {
    RCLCPP_ERROR(get_logger(),"angle_min (%f) can't be larger than or equal to angle_max (%f)", angle_min_, angle_max_);
    exit(-1);
  }
  if (angle_min_ < ANGLE_MIN_LIMIT) {
    RCLCPP_ERROR(get_logger(),"angle_min is set to %f while its minimum allowed value is %f", angle_min_, ANGLE_MIN_LIMIT);
    exit(-1);
  }
  if (angle_max_ > ANGLE_MAX_LIMIT) {
    RCLCPP_ERROR(get_logger(),"angle_max is set to %f while its maximum allowed value is %f", angle_max_, ANGLE_MAX_LIMIT);
    exit(-1);
  }
  if (!(range_min_ < range_max_)) {
    RCLCPP_ERROR(get_logger(),"range_min (%f) can't be larger than or equal to range_max (%f)", range_min_, range_max_);
    exit(-1);
  }
  if (range_min_ < RANGE_MIN_LIMIT) {
    RCLCPP_ERROR(get_logger(),"range_min is set to %f while its minimum allowed value is %f", range_min_, RANGE_MIN_LIMIT);
    exit(-1);
  }
  if (range_max_ > RANGE_MAX_LIMIT) {
    RCLCPP_ERROR(get_logger(),"range_max is set to %f while its maximum allowed value is %f", range_max_, RANGE_MAX_LIMIT);
    exit(-1);
  }
  if (average_factor_ <= 0 || average_factor_ > 8) {
    RCLCPP_ERROR(get_logger(),"average_factor is set to %d while its valid value is between 1 and 8", average_factor_);
    exit(-1);
  }
}

void LidarDriver::run()
{
  std::unique_lock<std::mutex> lock(mutex_);
  laser_scan_publisher_ = this->create_publisher<sensor_msgs::msg::LaserScan>("scan", 16);


//  auto handler = [this](const std::shared_ptr<std_srvs::srv::Empty::Request> request, const std::shared_ptr<std_srvs::srv::Empty::Response> response) -> void

//         {

  // ????????? cbg
  callback_group_service_ = this->create_callback_group(rclcpp::CallbackGroupType::MutuallyExclusive);
  query_serial_service_ = this->create_service<ltme_interfaces::srv::QuerySerial>("query_serial",
                            std::bind(&LidarDriver::querySerialService,this,std::placeholders::_1, std::placeholders::_2));
  query_firmware_service_ = this->create_service<ltme_interfaces::srv::QueryFirmwareVersion>("query_firmware_version",
                            std::bind(&LidarDriver::queryFirmwareVersion,this,std::placeholders::_1, std::placeholders::_2));
  query_hardware_service_ = this->create_service<ltme_interfaces::srv::QueryHardwareVersion>("query_hardware_version",
                            std::bind(&LidarDriver::queryHardwareVersion,this,std::placeholders::_1, std::placeholders::_2));
  request_hibernation_service_ = this->create_service<std_srvs::srv::Empty>("request_hibernation",
                            std::bind(&LidarDriver::requestHibernationService,this,std::placeholders::_1, std::placeholders::_2));
  // request_wake_up_service_ = this->create_service<std_srvs::srv::Empty>("request_wake_up",
  //                           std::bind(&LidarDriver::requestWakeUpService,this,std::placeholders::_1, std::placeholders::_2));
  // quit_driver_service_ = this->create_service<std_srvs::srv::Empty>("quit_driver",
  //                           std::bind(&LidarDriver::quitDriverService,this,std::placeholders::_1, std::placeholders::_2));

  // ros::AsyncSpinner spinner(1);
  // spinner.start();

  std::string address_str = device_address_;
  std::string port_str = "2105";

  size_t position = device_address_.find(':');
  if (position != std::string::npos) {
    address_str = device_address_.substr(0, position);
    port_str = device_address_.substr(position + 1);
  }

  in_addr_t address = htonl(INADDR_NONE);
  in_port_t port = 0;
  try {
    address = inet_addr(address_str.c_str());
    if (address == htonl(INADDR_NONE))
      throw std::exception();
    port = htons(std::stoi(port_str));
  }
  catch (...) {
    RCLCPP_ERROR(get_logger(),"Invalid device address: %s", device_address_.c_str());
    exit(-1);
  }

  ldcp_sdk::NetworkLocation location(address, port);
  device_ = std::unique_ptr<ldcp_sdk::Device>(new ldcp_sdk::Device(location));

  rclcpp::Rate loop_rate(0.3);
  while (rclcpp::ok() && !quit_driver_.load()) {
    if (device_->open() == ldcp_sdk::no_error) {
      hibernation_requested_ = false;

      lock.unlock();

      RCLCPP_INFO(get_logger(),"Device opened");

      bool reboot_required = false;
      if (enforced_transport_mode_ != "none") {
        std::string firmware_version;
        if (device_->queryFirmwareVersion(firmware_version) == ldcp_sdk::no_error) {
          if (firmware_version < "0201")
            RCLCPP_WARN(get_logger(),"Firmware version %s supports normal transport mode only, "
              "\"enforced_transport_mode\" parameter will be ignored", firmware_version.c_str());
          else {
            bool oob_enabled = false;
            if (device_->isOobEnabled(oob_enabled) == ldcp_sdk::no_error) {
              if ((enforced_transport_mode_ == "normal" && oob_enabled) ||
                  (enforced_transport_mode_ == "oob" && !oob_enabled)) {
                RCLCPP_INFO(get_logger(),"Transport mode will be switched to \"%s\"", oob_enabled ? "normal" : "oob");
                device_->setOobEnabled(!oob_enabled);
                device_->persistSettings();
                reboot_required = true;
              }
            }
            else
              RCLCPP_WARN(get_logger(),"Unable to query device for its current transport mode, "
                "\"enforced_transport_mode\" parameter will be ignored");
          }
        }
        else
          RCLCPP_WARN(get_logger(),"Unable to query device for firmware version, \"enforced_transport_mode\" parameter will be ignored");
      }

      if (!reboot_required) {
        int scan_frequency = DEFAULT_SCAN_FREQUENCY;
        if (scan_frequency_override_ != 0)
          scan_frequency = scan_frequency_override_;
        else {
          if (device_->getScanFrequency(scan_frequency) != ldcp_sdk::no_error)
            RCLCPP_WARN(get_logger(),"Unable to query device for scan frequency and will use %d as the frequency value", scan_frequency);
        }

        device_->startMeasurement();
        device_->startStreaming();

        int beam_count = 0;
        switch (scan_frequency) {
          case 10: beam_count = 3072; break;
          case 15: beam_count = 2048; break;
          case 20: beam_count = 1536; break;
          case 25:
          case 30: beam_count = 1024; break;
          default: beam_count = 2048; break;
        }
        int beam_index_min = std::ceil(angle_min_ * beam_count / (2 * M_PI));
        int beam_index_max = std::floor(angle_max_ * beam_count / (2 * M_PI));
        int beam_index_excluded_min = std::ceil(angle_excluded_min_ * beam_count / (2 * M_PI));
        int beam_index_excluded_max = std::floor(angle_excluded_max_ * beam_count / (2 * M_PI));

        sensor_msgs::msg::LaserScan laser_scan;
        laser_scan.header.frame_id = frame_id_;
        laser_scan.angle_min = angle_min_;
        laser_scan.angle_max = angle_max_;
        laser_scan.angle_increment = 2 * M_PI / beam_count * average_factor_;
        laser_scan.time_increment = 1.0 / scan_frequency / beam_count * average_factor_;
        laser_scan.scan_time = 1.0 / scan_frequency;
        laser_scan.range_min = range_min_;
        laser_scan.range_max = range_max_;

        auto readScanBlock = [&](ldcp_sdk::ScanBlock& scan_block) {
          if (device_->readScanBlock(scan_block) != ldcp_sdk::no_error)
            throw std::exception();
        };

        auto updateLaserScan = [&](const ldcp_sdk::ScanBlock& scan_block) {
          int block_size = scan_block.layers[0].ranges.size();
          for (int i = 0; i < block_size; i++) {
            int beam_index = block_size * (scan_block.block_id - 4) + i;
            if (beam_index < beam_index_min || beam_index > beam_index_max)
              continue;
            if (beam_index >= beam_index_excluded_min && beam_index <= beam_index_excluded_max)
              continue;
            if (scan_block.layers[0].ranges[i] != 0) {
              laser_scan.ranges[beam_index - beam_index_min] = scan_block.layers[0].ranges[i] * 0.002;
              laser_scan.intensities[beam_index - beam_index_min] = scan_block.layers[0].intensities[i];
            }
            else {
              laser_scan.ranges[beam_index - beam_index_min] = std::numeric_limits<float>::infinity();
              laser_scan.intensities[beam_index - beam_index_min] = 0;
            }
          }
        };

        while (rclcpp::ok() && !quit_driver_.load()) {
          laser_scan.ranges.resize(beam_index_max - beam_index_min + 1);
          laser_scan.intensities.resize(beam_index_max - beam_index_min + 1);

          std::fill(laser_scan.ranges.begin(), laser_scan.ranges.end(), 0.0);
          std::fill(laser_scan.intensities.begin(), laser_scan.intensities.end(), 0.0);

          ldcp_sdk::ScanBlock scan_block;
          try {
            do {
              readScanBlock(scan_block);
            } while (scan_block.block_id != 0);

            laser_scan.header.stamp = this->get_clock()->now();

            while (scan_block.block_id != 8 - 1) {
              updateLaserScan(scan_block);
              readScanBlock(scan_block);
            }
            updateLaserScan(scan_block);

            if (average_factor_ != 1) {
              int final_size = laser_scan.ranges.size() / average_factor_;
              for (int i = 0; i < final_size; i++) {
                double ranges_total = 0, intensities_total = 0;
                int count = 0;
                for (int j = 0; j < average_factor_; j++) {
                  int index = i * average_factor_ + j;
                  if (laser_scan.ranges[index] != 0) {
                    ranges_total += laser_scan.ranges[index];
                    intensities_total += laser_scan.intensities[index];
                    count++;
                  }
                }

                if (count > 0) {
                  laser_scan.ranges[i] = ranges_total / count;
                  laser_scan.intensities[i] = (int)(intensities_total / count);
                }
                else {
                  laser_scan.ranges[i] = 0;
                  laser_scan.intensities[i] = 0;
                }
              }

              laser_scan.ranges.resize(final_size);
              laser_scan.intensities.resize(final_size);
            }

            laser_scan_publisher_->publish(laser_scan);

            if (hibernation_requested_.load()) {
              device_->stopMeasurement();
              RCLCPP_INFO(get_logger(),"Device brought into hibernation");
              rclcpp::Rate loop_rate(10);
              while (hibernation_requested_.load())
                loop_rate.sleep();
              device_->startMeasurement();
              RCLCPP_INFO(get_logger(),"Woken up from hibernation");
            }
          }
          catch (const std::exception&) {
            RCLCPP_WARN(get_logger(),"Error reading data from device");
            break;
          }
        }

        device_->stopStreaming();
      }
      else
        device_->reboot();

      lock.lock();
      device_->close();

      if (!reboot_required)
        RCLCPP_INFO(get_logger(),"Device closed");
      else
        RCLCPP_INFO(get_logger(),"Device rebooted");
    }
    else {
      RCLCPP_ERROR(get_logger(),"Waiting for device... [%s]", device_address_.c_str());
      loop_rate.sleep();
    }
  }
}

void LidarDriver::querySerialService(const ltme_interfaces::srv::QuerySerial::Request::SharedPtr request,
                                     const ltme_interfaces::srv::QuerySerial::Response::SharedPtr response) {
  std::unique_lock<std::mutex> lock(mutex_, std::try_to_lock);
  if (lock.owns_lock()) {
    std::string serial;
    if (device_->querySerial(serial) == ldcp_sdk::no_error) {
      response->serial = serial;
      // return true;
    }
  }
  else{
    // return false;
    RCLCPP_ERROR(get_logger(),"querySerialService failed");
  }
}

void LidarDriver::queryFirmwareVersion(const ltme_interfaces::srv::QueryFirmwareVersion::Request::SharedPtr request,
                            const ltme_interfaces::srv::QueryFirmwareVersion::Response::SharedPtr response) {
  std::unique_lock<std::mutex> lock(mutex_, std::try_to_lock);
  if (lock.owns_lock()) {
    std::string firmware_version;
    if (device_->queryFirmwareVersion(firmware_version) == ldcp_sdk::no_error) {
      response->firmware_version = firmware_version;
      // return true;
    }
  }
  else{
    // return false;
    RCLCPP_ERROR(get_logger(),"queryFirmwareVersion failed");
  }
}

void LidarDriver::queryHardwareVersion(const ltme_interfaces::srv::QueryHardwareVersion::Request::SharedPtr request,
                            const ltme_interfaces::srv::QueryHardwareVersion::Response::SharedPtr response) {
  std::unique_lock<std::mutex> lock(mutex_, std::try_to_lock);
  if (lock.owns_lock()) {
    std::string hardware_version;
    if (device_->queryHardwareVersion(hardware_version) == ldcp_sdk::no_error) {
      response->hardware_version = hardware_version;
      // return true;
    }
  }
  else{
    // return false;
    RCLCPP_ERROR(get_logger(),"queryHardwareVersion failed"); 
  }
}

void LidarDriver::requestHibernationService(const std::shared_ptr<std_srvs::srv::Empty::Request> request, 
                                            const std::shared_ptr<std_srvs::srv::Empty::Response> response) {
  std::unique_lock<std::mutex> lock(mutex_, std::try_to_lock);
  if (lock.owns_lock()) {
    hibernation_requested_ = { true };
    // return true;
  }
  else{
    // return false;
    RCLCPP_ERROR(get_logger(),"requestHibernationService failed"); 
  }
}

void LidarDriver::requestWakeUpService(std_srvs::srv::Empty::Request::SharedPtr request,
                            std_srvs::srv::Empty::Response::SharedPtr response) {
  std::unique_lock<std::mutex> lock(mutex_, std::try_to_lock);
  if (lock.owns_lock()) {
    hibernation_requested_ = { false };
    // return true;
  }
  else{
    // return false;
    RCLCPP_ERROR(get_logger(),"requestWakeUpService failed"); 
  }
}

void LidarDriver::quitDriverService(std_srvs::srv::Empty::Request::SharedPtr request,
                         std_srvs::srv::Empty::Response::SharedPtr response) {
  quit_driver_ = { true };
  // return true;
}

int main(int argc, char* argv[])
{
  // ros::init(argc, argv, "ltme_node");
  rclcpp::init(argc, argv);
  // RCLCPP_INFO(get_logger(),"ltme_node started");

  auto node = std::make_shared<LidarDriver>("ltme_node");
  node->run();

  /* ????????????????????????????????????*/
  rclcpp::spin(node);
  rclcpp::shutdown();
  return 0;
}
