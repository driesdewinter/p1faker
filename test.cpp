/*
 * test.cpp
 *
 *  Created on: May 13, 2023
 *      Author: dries
 */

#include "core.h"
#include "config.h"
#include "logf.h"
#include <chrono>
#include <thread>

using namespace std::literals::chrono_literals;

namespace
{

config::param<int> p1("p1", 33);
config::param<int> p2("p2", 44);

struct policyimpl : core::policy
{
    policyimpl(const char* name) : core::policy(name) {}

    core::budget apply(const core::situation&){}
};

struct cons : core::consumer
{
    cons() : core::consumer("testje") {}

    void handle(const core::budget&, const core::situation&) {
        config::set_param("p3", "456");
        config::set_param("p1", "123");

        config::param<int> p3("p3", 55);

        logfinfo("p1=%s p2=%s p3=%s", p1.get(), p2.get(), p3.get());


    }
} conssss;

policyimpl impl1("red");
policyimpl impl2("orange");
policyimpl impl3("orange");


}


