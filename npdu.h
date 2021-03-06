#pragma once

#include <stdint.h>

class CemiFrame;
class TPDU;

class NPDU
{
    friend class CemiFrame;
public:
    NPDU(uint8_t* data, CemiFrame& frame);

    uint8_t octetCount() const;
    void octetCount(uint8_t value);
    uint8_t length() const;
    uint8_t hopCount() const;
    void hopCount(uint8_t value);
    CemiFrame& frame();
    TPDU& tpdu();
private:
    uint8_t* _data;
    CemiFrame& _frame;
};