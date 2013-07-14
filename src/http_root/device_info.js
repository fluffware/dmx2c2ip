
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
	    row.find(".replace-content")
		.each(function ()
		      {
			  var v = attrs[$(this).text()];
			  if (v != null) {
			      if (typeof(v) == "object") {
				  $(this).text(JSON.stringify(v));
					} else {
					    $(this).text(v);
					}
			  } else {
			      $(this).text("");
			  }
		      });
	    row.find(".set-path")
		.each(function ()
		      {
			  $(this).attr("path", value_prefix+id);
		      });
	}
	table.find("tr.remove-row").remove();
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
		var label;
		switch(type) {
		case "camera":
		    label="Camera "+id;
		    break;
		case "base":
		    label="Base station "+id;
		    break;
		case "camera":
		    label="OCP "+id;
		    break;
		default:
		    label=type+" "+id;
		    break;
		}
		var path = id+'/'+type;
		var new_option = templ.clone();
		new_option.attr("value", path);
		new_option.text(label);
		sel.append(new_option);
	    }
	}
	if (done) {
	    done(sel.find(':selected').val());
	}
    }
}