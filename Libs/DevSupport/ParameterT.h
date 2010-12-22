/*
 * Copyright (C) 2010, Freescale Semiconductor, Inc. All Rights Reserved.
 * THIS SOURCE CODE IS CONFIDENTIAL AND PROPRIETARY AND MAY NOT
 * BE USED OR DISTRIBUTED WITHOUT THE WRITTEN PERMISSION OF
 * Freescale Semiconductor, Inc.
 *
 */
// PropertyT.h
//
//////////////////////////////////////////////////////////////////////

#if _MSC_VER > 1000
#pragma once
#endif // _MSC_VER > 1000

#include "Libs/Public/StdString.h"
#include "Common/StdInt.h"
#include <map>

namespace property
{
	class Parameter
	{
	public:
		typedef std::map<CStdString, Parameter*> ParamMap;
		Parameter(LPCTSTR desc = NULL)
			: Desc(desc)
		{};
		virtual const CStdString ToString() const = 0;
		virtual int32_t Parse(CStdString str) = 0;
		virtual StdStringArray GetValueStrings() = 0;
		CStdString Desc;
		ParamMap map;
		bool IsContainer() { return map.size() > 0; };
//        virtual uint32_t StructSize() = 0;
	};

	template <typename T>
	class ParameterT : public Parameter
	{
	public:
		ParameterT(T value = (T)0, T valDefault = (T)0, LPCTSTR desc = NULL) 
			: Parameter(desc)
            , Value(value)
            , Default(valDefault)
		{};

/*		uint32_t StructSize()
        {
            if ( !IsContainer() )
                return sizeof(Value);
            else
            {
                uint32_t structSize = 0;
                ParamMap::iterator iter;
                for ( iter=map.begin(); iter!=map.end(); ++iter )
                {
                    structSize += (*iter).second->StructSize();
                }
                return structSize;
            }
        }
*/
        const CStdString ToString() const
		{
			CStdString str, fmt;
			fmt.Format(_T("0x%%0%dX"), 2*sizeof(T));

			if ( ValueList.empty() )
			{
				str.Format(fmt, Value);
			}
			else
			{
                std::map<T, CStdString>::const_iterator key;
                key = ValueList.find(Value);
                if ( key == ValueList.end() )
                    str = _T("Not found.");
                else
                    str = key->second;
			}

			return str;
		}

// this doesn't work because __time64_t resolves to __int64 which is handled by the primary template
/*        template <__time64_t>
        const CStdString ToString() const
        {
            CStdString dateStr; 
            struct tm modTime;
            if ( Value != 0 )
            {
                _gmtime64_s(&modTime, &Value);
                _tcsftime( dateStr.GetBufferSetLength(_MAX_PATH), _MAX_PATH, _T("%c"), &modTime);
                dateStr.ReleaseBuffer();
            }
            else
            {
                dateStr = _T("N/A");
            }

            return dateStr;
        }
*/
        int32_t Parse(CStdString str)
		{
			int32_t ret = FALSE;

			if ( ValueList.empty() )
			{
				_stscanf_s(str.GetBuffer(), _T("%i"), &Value);
			}
			else
			{
				std::map<T, CStdString>::iterator pair;
				for ( pair = ValueList.begin(); pair != ValueList.end(); ++pair )
				{
					if ( str.Compare((*pair).second) == 0 )
					{
						Value = (*pair).first;
						ret = TRUE;
						break;
					}
				}
			}

			return ret;
		}

		StdStringArray GetValueStrings()
		{
			StdStringArray strArray;
			std::map<T, CStdString>::iterator pair;
			for ( pair = ValueList.begin(); pair != ValueList.end(); ++pair )
			{
				strArray.push_back((*pair).second);
			}
			return strArray;
		}

		T Value;
		T Default;
		std::map<T, CStdString> ValueList;
	};

	int32_t ParseParameterString(LPCTSTR stringToParse, Parameter::ParamMap& paramMap);
/*	{
		int32_t numParamsParsed = 0;

		// Parameter=Type:Hid,Vid:0x066F,Pid:0x1234,TimeOut:10
		CStdString theString(stringToParse);

		// Holder for Key:Value pair, Key part, Value part
		CStdString pairStr, keyStr, valueStr;

		// multiple Parameters are sepatated by commas
		while ( theString.GetLength() )
		{
			// get the key:value pair string
			int commaPos;
			if ( (commaPos = theString.Find(_T(','))) != -1 )
			{
				// get the string part up to the comma (key:value pair)
				pairStr = theString.Left(commaPos+1);
				// trime theString for the next round
				theString = theString.Right(theString.GetLength()-(commaPos+1));
			}
			else
			{
				// put the whole string in the pairStr
				pairStr = theString;
				// done
				theString.erase();
			}

			// get the Key and Value strings
			int colonPos;
			if ( (colonPos = pairStr.Find(_T(':'))) != -1 )
			{
				// get the string part up to the colon (key:value pair)
				keyStr = pairStr.Left(colonPos+1);
				// the Value is after the colon
				valueStr = pairStr.Right(pairStr.GetLength()-(colonPos+1));
			}
			else
			{
				;// Warning: no Key for this Key:Value pair
			}
			
			// Parse the Value into the ParameterT
			if( !keyStr.IsEmpty() )
			{
				numParamsParsed += paramMap[keyStr]->Parse(valueStr);
			}
		}
		
		return numParamsParsed;
	};
*/
} // namespace property

using namespace property;
