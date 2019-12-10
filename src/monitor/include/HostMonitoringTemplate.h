/* -------------------------------------------------------------------------- */
/* Copyright 2002-2019, OpenNebula Project, OpenNebula Systems                */
/*                                                                            */
/* Licensed under the Apache License, Version 2.0 (the "License"); you may    */
/* not use this file except in compliance with the License. You may obtain    */
/* a copy of the License at                                                   */
/*                                                                            */
/* http://www.apache.org/licenses/LICENSE-2.0                                 */
/*                                                                            */
/* Unless required by applicable law or agreed to in writing, software        */
/* distributed under the License is distributed on an "AS IS" BASIS,          */
/* WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.   */
/* See the License for the specific language governing permissions and        */
/* limitations under the License.                                             */
/* -------------------------------------------------------------------------- */
#ifndef HOST_MONITORING_TEMPLATE_H_
#define HOST_MONITORING_TEMPLATE_H_

#include "Template.h"

/**
 *  CapacityMonitoring stores host monitoring values like
 *  - FREE_CPU
 *  - USED_CPU
 *  - FREE_MEMORY
 *  - USED_MEMORY
 */
class CapacityMonitoring : public Template
{
public:
    CapacityMonitoring()
        : Template(true, '=', "CAPACITY")
    {}

    int from_template(const Template &tmpl);

    // todo if needed add attribute getters and setters:
    // uint64_t used_cpu() const
    // void set_used_cpu(uint64_t used_cpu)
};

/**
 *  DatastoresMonitoring stores info about host datastores, the scheme is:
 *  <DS>
 *      <ID>
 *      <FREE_MB>
 *      <USED_MB>
 */
class DatastoresMonitoring : public Template
{
public:
    DatastoresMonitoring()
        : Template(false, '=', "DATASTORES")
    {}

    int from_template(const Template &tmpl);
};

/**
 *  SystemMonitoring generic host monitoring info
 *  <DS>
 *      <ID>
 *      <FREE_MB>
 *      <USED_MB>
 */
class SystemMonitoring : public Template
{
public:
    SystemMonitoring()
        : Template(true, '=', "SYSTEM")
    {}

    int from_template(const Template &tmpl);
};

/**
 *  HostMonitoringTemplate stores all host monitoring info, divided to 3 main sections:
 *  - capacity
 *  - datastores
 *  - system
 */
class HostMonitoringTemplate
{
public:
    std::string to_xml() const;

    int oid() const { return _oid; }

    time_t timestamp() const { return _timestamp; }

    void timestamp(time_t timestamp) { _timestamp = timestamp; }

    /**
     *  Fills monitoring data from xml_string
     *  If some data are not contained, keep old data
     *  @return 0 on succes, -1 otherwise
     */
    int from_xml(const std::string& xml_string);

    int from_template(const Template &tmpl);

private:
    time_t _timestamp;
    int    _oid;

    CapacityMonitoring capacity;
    DatastoresMonitoring datastores;
    SystemMonitoring system;
};

#endif // HOST_MONITORING_TEMPLATE_H_
