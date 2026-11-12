// gameswf_xml.h      -- Rob Savoye <rob@welcomehome.org> 2005

// This source code has been donated to the Public Domain.  Do
// whatever you want with it.

// Implementation of ActionScript String class.


#include "base/tu_config.h"
#include "gameswf_string.h"
#include "gameswf_log.h"
#include "base/smart_ptr.h"


namespace gameswf
{
	void string_method(const fn_call& fn, const tu_stringi& method_name, const tu_string& this_string)
	// Executes the string method named by method_name.
	{
		if (method_name == "charCodeAt")
		{
			int	index = (int) fn.arg(0).to_number();
			if (index >= 0 && index < this_string.utf8_length())
			{
				fn.result->set_double(this_string.utf8_char_at(index));
				return;
			}

			fn.result->set_double(0);	// FIXME: according to docs, we're supposed to return "NaN"
			return;
		}
		else if (method_name == "charAt")
		{
			int	index = (int) fn.arg(0).to_number();
			if (index >= 0 && index < this_string.utf8_length())
			{
				tu_string result;
				result += this_string.utf8_char_at(index);
				fn.result->set_tu_string(result);
			}

			fn.result->set_string("");
			return;
		}
		else if (method_name == "fromCharCode")
		{
			// Takes a variable number of args.  Each arg
			// is a numeric character code.  Construct the
			// string from the character codes.

			tu_string result;

			for (int i = 0; i < fn.nargs; i++)
			{
				uint32 c = (uint32) fn.arg(i).to_number();
				result.append_wide_char(c);
			}

			fn.result->set_tu_string(result);
			return;
		}
		else if (method_name == "toUpperCase")
		{
			fn.result->set_tu_string(this_string.utf8_to_upper());
			return;
		}
		else if (method_name == "toLowerCase")
		{
			fn.result->set_tu_string(this_string.utf8_to_lower());
			return;
		}
		else if (method_name == "indexOf")
		{
			if (fn.nargs < 1)
			{
				fn.result->set_double(-1);
				return;
			}
			else
			{
				int	start_index = 0;
				if (fn.nargs > 1)
				{
					start_index = (int) fn.arg(1).to_number();
				}
				const char*	str = this_string.c_str();
				const char*	p = strstr(
					str + start_index,	// FIXME: not UTF-8 correct!
					fn.arg(0).to_string());
				if (p == NULL)
				{
					fn.result->set_double(-1);
					return;
				}

				fn.result->set_double(tu_string::utf8_char_count(str, p - str));
				return;
			}
		}
		else if (method_name == "substring")
		{
			// Pull a slice out of this_string.
			int	start = 0;
			int	utf8_len = this_string.utf8_length();
			int	end = utf8_len;
			if (fn.nargs >= 1)
			{
				start = (int) fn.arg(0).to_number();
				start = iclamp(start, 0, utf8_len);
			}
			if (fn.nargs >= 2)
			{
				end = (int) fn.arg(1).to_number();
				end = iclamp(end, 0, utf8_len);
			}

			if (end < start) swap(&start, &end);	// dumb, but that's what the docs say
			assert(end >= start);

			fn.result->set_tu_string(this_string.utf8_substring(start, end));
			return;
		}
		// OpenBattlefield2: the rest of SwiffPlayer's String table (its names lie
		// together in the DLL: fromCharCode substring toUpperCase toLowerCase
		// slice lastIndexOf indexOf concat charCodeAt charAt split — no substr).
		else if (method_name == "concat")
		{
			tu_string	out = this_string;
			for (int i = 0; i < fn.nargs; i++) out += fn.arg(i).to_tu_string();
			fn.result->set_tu_string(out);
			return;
		}
		else if (method_name == "lastIndexOf")
		{
			if (fn.nargs < 1)
			{
				fn.result->set_double(-1);
				return;
			}
			const tu_string	sought = fn.arg(0).to_tu_string();
			const char*	s = this_string.c_str();
			const char*	found = NULL;
			for (const char* p = strstr(s, sought.c_str()); p != NULL; p = strstr(p + 1, sought.c_str()))
			{
				found = p;
				if (*p == 0) break;
			}
			if (found == NULL)
			{
				fn.result->set_double(-1);
				return;
			}
			fn.result->set_int(tu_string(s, int(found - s)).utf8_length());
			return;
		}
		else if (method_name == "slice")
		{
			// Negative positions count from the end.
			const int	len = this_string.utf8_length();
			int	start = fn.nargs >= 1 ? (int) fn.arg(0).to_number() : 0;
			int	end = fn.nargs >= 2 ? (int) fn.arg(1).to_number() : len;
			if (start < 0) start += len;
			if (end < 0) end += len;
			start = iclamp(start, 0, len);
			end = iclamp(end, 0, len);
			fn.result->set_tu_string(end > start ? this_string.utf8_substring(start, end) : tu_string(""));
			return;
		}
		else if (method_name == "split")
		{
			// An Array of the pieces; an empty separator splits into
			// characters, none gives the whole string as one element.
			as_object*	pieces = new_array();
			if (fn.nargs < 1 || fn.arg(0).get_type() == as_value::UNDEFINED)
			{
				array_append(pieces, as_value(this_string));
			}
			else
			{
				const tu_string	separator = fn.arg(0).to_tu_string();
				const int	limit = fn.nargs >= 2 ? (int) fn.arg(1).to_number() : 0x7fffffff;
				int	count = 0;
				if (separator.length() == 0)
				{
					for (int i = 0, n = this_string.utf8_length(); i < n && count < limit; i++, count++)
					{
						array_append(pieces, as_value(this_string.utf8_substring(i, i + 1)));
					}
				}
				else
				{
					const char*	s = this_string.c_str();
					while (count < limit)
					{
						const char*	at = strstr(s, separator.c_str());
						if (at == NULL)
						{
							array_append(pieces, as_value(tu_string(s)));
							break;
						}
						array_append(pieces, as_value(tu_string(s, int(at - s))));
						count++;
						s = at + separator.length();
					}
				}
			}
			fn.result->set_as_object_interface(pieces);
			return;
		}

		fn.result->set_undefined();
	}



	struct tu_string_as_object : public gameswf::as_object
	{
		tu_string m_string;
		// OpenBattlefield2: a String object converts to its string (r777 gave
		// "[object Object]"), so `String(v).split(", ")` splits v — the
		// lists' column widths came out as one 0 and their rows invisible.
		virtual const char*	get_text_value() const { return m_string.c_str(); }
		// OpenBattlefield2: and it has a `length`. r777's `String(x)` makes an
		// object even when called as a function, so a list component's
		// callback name read through `String(...)` had no length, counted as
		// empty, and a selected row never called `selectProfile`.
		virtual bool	get_member(const tu_stringi& name, as_value* val)
		{
			if (name == "length")
			{
				val->set_int(m_string.utf8_length());
				return true;
			}
			return as_object::get_member(name, val);
		}
	};
  
  
	void string_last_index_of(const fn_call& fn)
	{
		tu_string_as_object* this_string_ptr = (tu_string_as_object*) fn.this_ptr;
		assert(this_string_ptr);

		// tulrich: Ugh!  The caller has done hash.get() on
		// the method name to find the method, and now we're
		// going to construct a new method name and run it
		// through a big if-else.
		//
		// TODO do this more efficiently.
		string_method(fn, tu_stringi("lastIndexOf"), this_string_ptr->m_string);
	}
  
	void string_from_char_code(const fn_call& fn)
	{
		tu_string_as_object* this_string_ptr = (tu_string_as_object*) fn.this_ptr;
		assert(this_string_ptr);

		// tulrich: Ugh!  The caller has done hash.get() on
		// the method name to find the method, and now we're
		// going to construct a new method name and run it
		// through a big if-else.
		//
		// TODO do this more efficiently.
		string_method(fn, tu_stringi("fromCharCode"), this_string_ptr->m_string);
	}


	void string_char_code_at(const fn_call& fn)
	{
		tu_string_as_object* this_string_ptr = (tu_string_as_object*) fn.this_ptr;
		assert(this_string_ptr);

		// tulrich: Ugh!  The caller has done hash.get() on
		// the method name to find the method, and now we're
		// going to construct a new method name and run it
		// through a big if-else.
		//
		// TODO do this more efficiently.
		string_method(fn, tu_stringi("charCodeAt"), this_string_ptr->m_string);
	}


	void string_to_string(const fn_call& fn)
	{
		tu_string_as_object* this_string_ptr = (tu_string_as_object*) fn.this_ptr;
		assert(this_string_ptr);

		fn.result->set_tu_string(this_string_ptr->m_string);
	}


	void string_ctor(const fn_call& fn)
	{
		smart_ptr<tu_string_as_object> str = new tu_string_as_object;

		if (fn.nargs > 0)
		{
			str->m_string = fn.arg(0).to_tu_string();
		}
		
		// TODO fill in the rest
		str->set_member("toString", &string_to_string);
		str->set_member("fromCharCode", &string_from_char_code);
		str->set_member("charCodeAt", &string_char_code_at);
		str->set_member("lastIndexOf", &string_last_index_of);
    
		fn.result->set_as_object_interface(str.get_ptr());
	}
  
} // namespace gameswf
