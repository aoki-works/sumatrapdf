function driver() {
  let selectors = ["input#cmd_ids", "input#key_sht", "input#cmd_plt"];
  let q =
    "//table[contains(@class,'collection-content')]/tbody/tr[not(./td/input)]";
  let rows = getElementByXpath(q);
  // console.log("rows:", rows.length);
  let lists = [],
    inputs = [];
  selectors.forEach((x, y) => {
    inputs[y] = document.querySelector(x);
  });
  for (let i = 1; i <= selectors.length; i++) {
    q =
      "//table[contains(@class,'collection-content')]/tbody/tr/td[(not(./input))][position()=" +
      i +
      "]";
    let els = getElementByXpath(q);
    els = els.map((x) => x.innerText);
    lists[i - 1] = els;
  }

  lists[1] = lists[1].map((x) => x.replace(/(?:(?<!\+)|(?<=\+\+))\,/g, "")); //removing commas b/w shortcuts
  function tableFilter() {
    let regexs = [
      getRegex_cmdids(inputs[0]),
      getRegex_keysht(inputs[1]),
      getRegex_cmdplt(inputs[2]),
    ];
    rows.forEach((row) => row.setAttribute("style", "display: none;"));
    let shortlist = new Array(rows.length).fill(undefined);
    regexs.forEach((regex, list_index) => {
      if (!!regex)
        lists[list_index].forEach((item, row_index) => {
          if (shortlist[row_index] === undefined)
            shortlist[row_index] = regex.test(item);
          else if (shortlist[row_index])
            shortlist[row_index] = regex.test(item);
        });
    });
    if (!regexs.some((x) => !!x))
      rows.forEach((row) => row.removeAttribute("style"));
    else
      shortlist.forEach((flag, index) => {
        if (flag) rows[index].removeAttribute("style");
      });
  }
  inputs.forEach((ele) => setEvent(ele, tableFilter));
}

function setEvent(target, callback) {
  target.addEventListener("keyup", callback);
}

function getElementByXpath(xpathToExecute) {
  let result = [];
  let snapshotNodes = document.evaluate(
    xpathToExecute,
    document,
    null,
    XPathResult.ORDERED_NODE_SNAPSHOT_TYPE,
    null
  );
  for (let i = 0; i < snapshotNodes.snapshotLength; i++)
    result.push(snapshotNodes.snapshotItem(i));
  return result;
}

function getRegex_cmdids(ele) {
  let ip_val = ele.value.replace(/([^\w\s])/g, "").replace(/\s+$/, "");
  if (ip_val.length == 0) return false;
  return new RegExp(ip_val.replace(/\s+(\w+)/g, "(?=.*$1)"), "i");
}

function getRegex_keysht(ele) {
  let ip_val = ele.value.replace(/\s+$/, "");
  if (ip_val.length == 0) return false;
  return new RegExp(
    "(?:(?=\\W)(?<=\\w)|(?<!\\w))(" +
      ip_val
        .replace(/([^\w\s])/g, "\\$1")
        .replace(/([^\s]+)/g, "($1)")
        .replace(/\s+/g, "|") +
      ")(?:(?<=\\W)(?=\\w)|(?!\\w))",
    "i"
  );
}

function getRegex_cmdplt(ele) {
  let ip_val = ele.value.replace(/\s+$/, "");
  if (ip_val.length == 0) return false;
  return new RegExp(
    "(?:(?=\\W)(?<=\\w)|(?<!\\w))" +
      ip_val
        .replace(/([^\w\s])/g, "\\$1")
        .replace(/\s+([\w\W]+)/g, "(?=.*\\b$1)"),
    "i"
  );
}
driver();
