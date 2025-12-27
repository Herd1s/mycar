% 创建数据单元格数组
data = {
    '符号', '含义', '正方向', '单位';
    'θ', '摆杆与竖直方向夹角', '图示为正方向', 'rad';
    'α', '摆杆与机体相对角度', '图示为正方向', 'rad';
    'φ', '机体与水平夹角', '图示为正方向', 'rad';
    'x', '驱动轮位移', '箭头所示', 'm';
    'xb', '腿部机构转轴位移', '同x', 'm';
    'T', '驱动轮输出力矩', '同θ', 'N·m';
    'Tp', '髋关节输出力矩', '同α', 'N·m';
    'N', '驱动轮对摆杆力的水平分量', '箭头所示', 'N';
    'P', '驱动轮对摆杆力的竖直分量', '箭头所示', 'N';
    'Nf', '地面对驱动轮摩擦力', '箭头所示', 'N';
    'NM', '摆杆对机体力水平方向分量', '箭头所示', 'N';
    'PM', '摆杆对机体力竖直方向分量', '箭头所示', 'N';
    'F', '摆杆推力', '向外为正', 'N'
};

% 写入Excel文件
filename = '符号说明表.xlsx';
writecell(data, filename);

% 设置单元格格式
try
    excel = actxserver('Excel.Application');
    workbook = excel.Workbooks.Open([pwd '\' filename]);
    sheets = workbook.Sheets;
    sheet = sheets.Item(1);
    
    % 设置标题行格式
    range = sheet.Range('A1:D1');
    range.Font.Bold = true;
    range.Interior.Color = hex2dec('CCCCCC');
    
    % 设置列宽
    sheet.Columns(1).ColumnWidth = 8;
    sheet.Columns(2).ColumnWidth = 25;
    sheet.Columns(3).ColumnWidth = 15;
    sheet.Columns(4).ColumnWidth = 8;
    
    % 保存并关闭
    workbook.Save;
    workbook.Close;
    excel.Quit;
catch
    disp('无法通过COM接口设置Excel格式，请手动调整列宽和标题格式');
end

disp(['已创建文件: ' filename]);