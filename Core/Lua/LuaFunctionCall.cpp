/**
 * Orthanc - A Lightweight, RESTful DICOM Store
 * Copyright (C) 2012-2015 Sebastien Jodogne, Medical Physics
 * Department, University Hospital of Liege, Belgium
 *
 * This program is free software: you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation, either version 3 of the
 * License, or (at your option) any later version.
 *
 * In addition, as a special exception, the copyright holders of this
 * program give permission to link the code of its release with the
 * OpenSSL project's "OpenSSL" library (or with modified versions of it
 * that use the same license as the "OpenSSL" library), and distribute
 * the linked executables. You must obey the GNU General Public License
 * in all respects for all of the code used other than "OpenSSL". If you
 * modify file(s) with this exception, you may extend this exception to
 * your version of the file(s), but you are not obligated to do so. If
 * you do not wish to do so, delete this exception statement from your
 * version. If you delete this exception statement from all source files
 * in the program, then also delete it here.
 * 
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program. If not, see <http://www.gnu.org/licenses/>.
 **/


#include "../PrecompiledHeaders.h"
#include "LuaFunctionCall.h"

#include <cassert>
#include <stdio.h>
#include <boost/lexical_cast.hpp>
#include <glog/logging.h>

namespace Orthanc
{
  void LuaFunctionCall::CheckAlreadyExecuted()
  {
    if (isExecuted_)
    {
      throw LuaException("Arguments cannot be pushed after the function is executed");
    }
  }

  LuaFunctionCall::LuaFunctionCall(LuaContext& context,
                                   const char* functionName) : 
    context_(context),
    isExecuted_(false)
  {
    // Clear the stack to fulfill the invariant
    lua_settop(context_.lua_, 0);
    lua_getglobal(context_.lua_, functionName);
  }

  void LuaFunctionCall::PushString(const std::string& value)
  {
    CheckAlreadyExecuted();
    lua_pushstring(context_.lua_, value.c_str());
  }

  void LuaFunctionCall::PushBoolean(bool value)
  {
    CheckAlreadyExecuted();
    lua_pushboolean(context_.lua_, value);
  }

  void LuaFunctionCall::PushInteger(int value)
  {
    CheckAlreadyExecuted();
    lua_pushinteger(context_.lua_, value);
  }

  void LuaFunctionCall::PushDouble(double value)
  {
    CheckAlreadyExecuted();
    lua_pushnumber(context_.lua_, value);
  }

  void LuaFunctionCall::PushJson(const Json::Value& value)
  {
    CheckAlreadyExecuted();
    context_.PushJson(value);
  }

  void LuaFunctionCall::ExecuteInternal(int numOutputs)
  {
    CheckAlreadyExecuted();

    assert(lua_gettop(context_.lua_) >= 1);
    int nargs = lua_gettop(context_.lua_) - 1;
    int error = lua_pcall(context_.lua_, nargs, numOutputs, 0);

    if (error) 
    {
      assert(lua_gettop(context_.lua_) >= 1);
          
      std::string description(lua_tostring(context_.lua_, -1));
      lua_pop(context_.lua_, 1); /* pop error message from the stack */
      throw LuaException(description);
    }

    if (lua_gettop(context_.lua_) < numOutputs)
    {
      throw LuaException("The function does not give the expected number of outputs");
    }

    isExecuted_ = true;
  }

  bool LuaFunctionCall::ExecutePredicate()
  {
    ExecuteInternal(1);
    
    if (!lua_isboolean(context_.lua_, 1))
    {
      throw LuaException("The function is not a predicate (only true/false outputs allowed)");
    }

    return lua_toboolean(context_.lua_, 1) != 0;
  }


  static void PopJson(Json::Value& result,
                      lua_State* lua,
                      int top)
  {
    if (lua_istable(lua, top))
    {
      Json::Value tmp = Json::objectValue;
      bool isArray = true;
      size_t size = 0;

      // http://stackoverflow.com/a/6142700/881731
      
      // Push another reference to the table on top of the stack (so we know
      // where it is, and this function can work for negative, positive and
      // pseudo indices
      lua_pushvalue(lua, top);
      // stack now contains: -1 => table
      lua_pushnil(lua);
      // stack now contains: -1 => nil; -2 => table
      while (lua_next(lua, -2))
      {
        // stack now contains: -1 => value; -2 => key; -3 => table
        // copy the key so that lua_tostring does not modify the original
        lua_pushvalue(lua, -2);
        // stack now contains: -1 => key; -2 => value; -3 => key; -4 => table
        std::string key(lua_tostring(lua, -1));
        Json::Value v;
        PopJson(v, lua, -2);

        tmp[key] = v;

        size += 1;
        try
        {
          if (boost::lexical_cast<size_t>(key) != size)
          {
            isArray = false;
          }
        }
        catch (boost::bad_lexical_cast&)
        {
          isArray = false;
        }
        
        // pop value + copy of key, leaving original key
        lua_pop(lua, 2);
        // stack now contains: -1 => key; -2 => table
      }
      // stack now contains: -1 => table (when lua_next returns 0 it pops the key
      // but does not push anything.)
      // Pop table
      lua_pop(lua, 1);

      // Stack is now the same as it was on entry to this function

      if (isArray)
      {
        result = Json::arrayValue;
        for (size_t i = 0; i < size; i++)
        {
          result.append(tmp[boost::lexical_cast<std::string>(i + 1)]);
        }
      }
      else
      {
        result = tmp;
      }
    }
    else if (lua_isnumber(lua, top))
    {
      result = static_cast<float>(lua_tonumber(lua, top));
    }
    else if (lua_isstring(lua, top))
    {
      result = std::string(lua_tostring(lua, top));
    }
    else if (lua_isboolean(lua, top))
    {
      result = static_cast<bool>(lua_toboolean(lua, top));
    }
    else
    {
      LOG(WARNING) << "Unsupported Lua type when returning Json";
      result = Json::nullValue;
    }
  }


  void LuaFunctionCall::ExecuteToJson(Json::Value& result)
  {
    ExecuteInternal(1);
    PopJson(result, context_.lua_, lua_gettop(context_.lua_));
  }
}
