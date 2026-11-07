#pragma once
namespace chompi {
inline void fill_led_data() {}
inline void SetPthLedFloat(int, float, float, float) {}
inline void SetSmtLedFloat(int, float, float, float) {}
} // namespace chompi
using chompi::SetPthLedFloat; using chompi::SetSmtLedFloat; using chompi::fill_led_data;
