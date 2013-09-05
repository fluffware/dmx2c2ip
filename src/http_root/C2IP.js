
function device_type_label(type)
{
    switch(type) {
    case "camera":
	return "Camera";
    case "base":
	return "Base station";
    case "OCP":
	return "OCP ";
    default:
	return type;
    }
}

function device_label(name, type)
{
    return device_type_label(type)+' '+name;
}

function get_query_params(url)
{
    var parts = url.split('?');
    if (parts.length < 2) return {};
    params = parts[1].split('&');
    var v = {};
    for (p in params) {
	kv = params[p].split("=");
	v[kv[0]] = kv[1];
    }
    return v;
}

function replace_content(map, top)
{
    top.find("[replace-content]")
	.each(function ()
	      {
		  var v = map[$(this).attr("replace-content")];
		  if (v != null) {
		      if (typeof(v) == "object") {
			  $(this).text(JSON.stringify(v));
		      } else {
			  $(this).text(v);
		      }
		  }
	      });
}

function ExpandTable(table, url, value_prefix)
{
    var url = url;
    var table = table;
    var value_prefix = value_prefix;
    var row = $(table).find("tr.template");
    row.hide();
    var template = row.clone();
    template.hide();
    template.removeClass("template");
    requestUpdate(this);

    function requestUpdate(upd) 
    {
	jQuery.getJSON(url, handleValueReply).error(function() {requestError(upd)});
    }

    function requestUpdateDone(upd) 
    {
	upd.tout = setTimeout(function() {requestUpdate(upd)}, 1000);
    }
    
    function handleValueReply(data,status,req)
    {
	table.find("tr[row-id]").remove();
	for (id in data) {
	    var row = template.clone();
	    row.attr("row-id", id);
	    table.append(row);
	    row.show();
	    var attrs = data[id];
	    attrs["id"] = id; 
	    replace_content(attrs, row);
	    row.find(".set-path")
		.each(function ()
		      {
			  var suffix = $(this).attr("path-suffix");
			  if (suffix)
			      suffix = '/' + suffix;
			  else
			      suffix='';
			  $(this).attr("path", value_prefix+id+suffix);
		      });
	}
	table.find("tr.remove-row").remove();
    }

    function requestError(req,status,error)
    {
    }
}

function FillTable(table, url, filter)
{
    var url = url;
    var table = table;
    var value_prefix = value_prefix;

    requestUpdate(this);

    function requestUpdate(upd) 
    {
	jQuery.getJSON(url, handleValueReply).error(function() {requestError(upd)});
    }

    function requestUpdateDone(upd) 
    {
	upd.tout = setTimeout(function() {requestUpdate(upd)}, 1000);
    }
    
    function handleValueReply(data,status,req)
    {
	var rows = table.find("tr[row-id]");
	rows.each(function(i,e) {
	    var id = $(e).attr('row-id');
	    replace_content(data[id], $(e));
	});
    }

    function requestError(req,status,error)
    {
    }
}

function SetDeviceOptions(url_arg, select_element, done)
{
    var url = url_arg;
    var sel = select_element;

    jQuery.getJSON(url, handleValueReply);

    function handleValueReply(values,status,req)
    {
	console.log($(sel.find('option:first')));
	var templ = sel.find('option:first').detach();
	sel.find('option').remove();
	for (id in values) {
	    for (type in values[id]) {

		var path = id+'/'+type;
		var new_option = templ.clone();
		new_option.attr("value", path);
		new_option.text(device_label(id, type));
		sel.append(new_option);
	    }
	}
	if (done) {
	    done(sel.find(':selected').val());
	}
    }
}