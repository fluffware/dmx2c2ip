
function ExpandTable(table, url, value_prefix)
{
    var url = url;
    var table = table;
    var value_prefix = value_prefix;
    var template = $(table).find("tr[class = 'template']");
    template.hide();
    template.removeAttr("class");
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
	console.log("Reply");
	for (id in data) {
	    var row = table.find("tr[row-id = "+id+"]");
	    if (row.empty()) {
		row = template.clone();
		row.attr("row-id", id);
	    }
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
    }

    function requestError(req,status,error)
    {
    }
}
