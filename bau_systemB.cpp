#include "bau_systemB.h"
#include <string.h>
#include <stdio.h>

BauSystemB::BauSystemB(Platform& platform): _memoryReference((uint8_t*)&_deviceObj), _memory(platform), _addrTable(_memoryReference),
    _assocTable(_memoryReference), _groupObjTable(_memoryReference), _appProgram(_memoryReference),
    _platform(platform), _appLayer(_assocTable, *this),
    _transLayer(_appLayer, _addrTable, _platform), _netLayer(_transLayer)
{
    _appLayer.transportLayer(_transLayer);
    _transLayer.networkLayer(_netLayer);
    _memory.addSaveRestore(&_deviceObj);
    _memory.addSaveRestore(&_appProgram);
    _memory.addSaveRestore(&_addrTable);
    _memory.addSaveRestore(&_assocTable);
    _memory.addSaveRestore(&_groupObjTable);
}

void BauSystemB::loop()
{
    dataLinkLayer().loop();
    _transLayer.loop();
    sendNextGroupTelegram();
}

bool BauSystemB::enabled()
{
    return dataLinkLayer().enabled();
}

void BauSystemB::enabled(bool value)
{
    dataLinkLayer().enabled(value);
}

void BauSystemB::sendNextGroupTelegram()
{
    static uint16_t startIdx = 1;

    GroupObjectTableObject& table = _groupObjTable;
    uint16_t objCount = table.entryCount();

    for (uint16_t asap = startIdx; asap < objCount; asap++)
    {
        GroupObject& go = table.get(asap);

        ComFlag flag = go.commFlag();
        if (flag != ReadRequest && flag != WriteRequest)
            continue;

        if(!go.communicationEnable() || ! go.transmitEnable())
            continue;

        if (flag == WriteRequest)
        {
            uint8_t* data = go.valueRef();
            _appLayer.groupValueWriteRequest(AckRequested, asap, go.priority(), NetworkLayerParameter, data,
                go.sizeInTelegram());
        }
        else
        {
            _appLayer.groupValueReadRequest(AckRequested, asap, go.priority(), NetworkLayerParameter);
        }

        go.commFlag(Transmitting);

        startIdx = asap + 1;
        return;
    }

    startIdx = 1;
}

void BauSystemB::updateGroupObject(GroupObject & go, uint8_t * data, uint8_t length)
{
    uint8_t* goData = go.valueRef();
    if (length != go.valueSize())
    {
        go.commFlag(Error);
        return;
    }

    memcpy(goData, data, length);

    go.commFlag(cfUpdate);
    if (go.updateHandler)
        go.updateHandler(go);
}

void BauSystemB::readMemory()
{
    _memory.readMemory();
}

void BauSystemB::writeMemory()
{
    _memory.writeMemory();
}

DeviceObject& BauSystemB::deviceObject()
{
    return _deviceObj;
}

GroupObjectTableObject& BauSystemB::groupObjectTable()
{
    return _groupObjTable;
}

ApplicationProgramObject& BauSystemB::parameters()
{
    return _appProgram;
}

bool BauSystemB::configured()
{
    return _groupObjTable.loadState() == LS_LOADED
        && _addrTable.loadState() == LS_LOADED
        && _assocTable.loadState() == LS_LOADED
        && _appProgram.loadState() == LS_LOADED;
}

void BauSystemB::deviceDescriptorReadIndication(Priority priority, HopCountType hopType, uint16_t asap, uint8_t descriptorType)
{
    if (descriptorType != 0)
        descriptorType = 0x3f;

    _appLayer.deviceDescriptorReadResponse(AckRequested, priority, hopType, asap, descriptorType, descriptor());
}

void BauSystemB::memoryWriteIndication(Priority priority, HopCountType hopType, uint16_t asap, uint8_t number,
    uint16_t memoryAddress, uint8_t * data)
{
    memcpy(_memoryReference + memoryAddress, data, number);
    _memory.memoryModified();

    if (_deviceObj.verifyMode())
        memoryReadIndication(priority, hopType, asap, number, memoryAddress);
}

void BauSystemB::memoryReadIndication(Priority priority, HopCountType hopType, uint16_t asap, uint8_t number,
    uint16_t memoryAddress)
{
    _appLayer.memoryReadResponse(AckRequested, priority, hopType, asap, number, memoryAddress,
        _memoryReference + memoryAddress);
}

void BauSystemB::restartRequestIndication(Priority priority, HopCountType hopType, uint16_t asap)
{
    // for platforms that don't really restart
    _deviceObj.progMode(false);

    // Flush the EEPROM before resetting
    _memory.writeMemory();
    _platform.restart();
}

void BauSystemB::authorizeIndication(Priority priority, HopCountType hopType, uint16_t asap, uint32_t key)
{
    _appLayer.authorizeResponse(AckRequested, priority, hopType, asap, 0);
}

void BauSystemB::userMemoryReadIndication(Priority priority, HopCountType hopType, uint16_t asap, uint8_t number, uint32_t memoryAddress)
{
    _appLayer.userMemoryReadResponse(AckRequested, priority, hopType, asap, number, memoryAddress,
        _memoryReference + memoryAddress);
}

void BauSystemB::userMemoryWriteIndication(Priority priority, HopCountType hopType, uint16_t asap, uint8_t number, uint32_t memoryAddress, uint8_t* data)
{
    memcpy(_memoryReference + memoryAddress, data, number);
    _memory.memoryModified();

    if (_deviceObj.verifyMode())
        userMemoryReadIndication(priority, hopType, asap, number, memoryAddress);
}

void BauSystemB::propertyDescriptionReadIndication(Priority priority, HopCountType hopType, uint16_t asap, uint8_t objectIndex,
    uint8_t propertyId, uint8_t propertyIndex)
{
    uint8_t pid = propertyId;
    bool writeEnable = false;
    uint8_t type = 0;
    uint16_t numberOfElements = 0;
    uint8_t access = 0;
    InterfaceObject* obj = getInterfaceObject(objectIndex);
    if (obj)
        obj->readPropertyDescription(pid, propertyIndex, writeEnable, type, numberOfElements, access);

    _appLayer.propertyDescriptionReadResponse(AckRequested, priority, hopType, asap, objectIndex, pid, propertyIndex,
        writeEnable, type, numberOfElements, access);
}

void BauSystemB::propertyValueWriteIndication(Priority priority, HopCountType hopType, uint16_t asap, uint8_t objectIndex,
    uint8_t propertyId, uint8_t numberOfElements, uint16_t startIndex, uint8_t* data, uint8_t length)
{
    InterfaceObject* obj = getInterfaceObject(objectIndex);
    if(obj)
        obj->writeProperty((PropertyID)propertyId, startIndex, data, numberOfElements);
    propertyValueReadIndication(priority, hopType, asap, objectIndex, propertyId, numberOfElements, startIndex);
}

void BauSystemB::propertyValueReadIndication(Priority priority, HopCountType hopType, uint16_t asap, uint8_t objectIndex,
    uint8_t propertyId, uint8_t numberOfElements, uint16_t startIndex)
{
    uint8_t size = 0;
    uint32_t elementCount = numberOfElements;
    InterfaceObject* obj = getInterfaceObject(objectIndex);
    if (obj)
    {
        uint8_t elementSize = obj->propertySize((PropertyID)propertyId);
        size = elementSize * numberOfElements;
    }
    else
        elementCount = 0;

    uint8_t data[size];
    if(obj)
        obj->readProperty((PropertyID)propertyId, startIndex, elementCount, data);
    _appLayer.propertyValueReadResponse(AckRequested, priority, hopType, asap, objectIndex, propertyId, elementCount,
        startIndex, data, size);
}

void BauSystemB::individualAddressReadIndication(HopCountType hopType)
{
    if (_deviceObj.progMode())
        _appLayer.individualAddressReadResponse(AckRequested, hopType);
}

void BauSystemB::individualAddressWriteIndication(HopCountType hopType, uint16_t newaddress)
{
    if (_deviceObj.progMode())
        _deviceObj.induvidualAddress(newaddress);
}

void BauSystemB::groupValueWriteLocalConfirm(AckType ack, uint16_t asap, Priority priority, HopCountType hopType, uint8_t * data, uint8_t dataLength, bool status)
{
    GroupObject& go = _groupObjTable.get(asap);
    if (status)
        go.commFlag(Ok);
    else
        go.commFlag(Error);
}

void BauSystemB::groupValueReadLocalConfirm(AckType ack, uint16_t asap, Priority priority, HopCountType hopType, bool status)
{
    GroupObject& go = _groupObjTable.get(asap);
    if (status)
        go.commFlag(Ok);
    else
        go.commFlag(Error);
}

void BauSystemB::groupValueReadIndication(uint16_t asap, Priority priority, HopCountType hopType)
{
    GroupObject& go = _groupObjTable.get(asap);
    uint8_t* data = go.valueRef();
    _appLayer.groupValueReadResponse(AckRequested, asap, priority, hopType, data, go.sizeInTelegram());
}

void BauSystemB::groupValueReadAppLayerConfirm(uint16_t asap, Priority priority, HopCountType hopType, uint8_t* data,
    uint8_t dataLength)
{
    GroupObject& go = _groupObjTable.get(asap);

    if (!go.communicationEnable() || !go.responseUpdateEnable())
        return;

    updateGroupObject(go, data, dataLength);
}

void BauSystemB::groupValueWriteIndication(uint16_t asap, Priority priority, HopCountType hopType, uint8_t * data, uint8_t dataLength)
{
    GroupObject& go = _groupObjTable.get(asap);

    if (!go.communicationEnable() || !go.writeEnable())
        return;

    updateGroupObject(go, data, dataLength);
}

void BauSystemB::addSaveRestore(SaveRestore* obj)
{
    _memory.addSaveRestore(obj);
}
